// Offline tests for the web interface's firmware update: webui/wchisp.js
// (the boot ROM client) against mock_boot_rom.js, its packets against
// minichlink's (ch32fun/minichlink/pgm-wch-isp.c), and the Firmware panel
// (webui/firmware_update.js + webusb.js) through a whole update with a
// mock badge and a mock navigator.usb.
//
//   node tools/isp_test/isp_test.js
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { MockBootRom, domError, inResult } = require('./mock_boot_rom');

const webui = path.join(__dirname, '..', '..', 'webui');
const { WchIsp, WchIspError, WCHISP } = require(path.join(webui, 'wchisp.js'));

// ---- a tiny test runner -----------------------------------------------------

const tests = [];
const test = (name, fn) => tests.push({ name, fn });
let checks = 0;
function ok(cond, what) {
	checks++;
	if (!cond) throw new Error(`failed: ${what}`);
}
function eq(a, b, what) {
	ok(a === b, `${what}: got ${JSON.stringify(a)}, expected ${JSON.stringify(b)}`);
}
function bytesEq(a, b, what) {
	ok(a.length === b.length && a.every((x, i) => x === b[i]), `${what}: [${hexs(a)}] != [${hexs(b)}]`);
}
async function rejects(promise, code, what) {
	try {
		await promise;
	} catch (e) {
		eq(e.code, code, `${what} (${e.message})`);
		return e;
	}
	throw new Error(`failed: ${what}: did not throw`);
}
const hexs = (b) => Array.from(b.slice(0, 24), (x) => x.toString(16).padStart(2, '0')).join(' ') + (b.length > 24 ? ' ...' : '');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(cond, what, ms = 3000) {
	const end = Date.now() + ms;
	while (!cond()) {
		if (Date.now() > end) throw new Error(`timed out waiting for ${what}`);
		await sleep(2);
	}
}

// ---- test data --------------------------------------------------------------

let seed = 7;
const rand = () => ((seed = (seed * 1103515245 + 12345) >>> 0) >>> 16) & 0xff;

// A firmware image of n bytes with the FW_INFO marker in it
function image(n, version = 'abc1234', layout = 'S1') {
	const img = Uint8Array.from({ length: n }, rand);
	img.set([0x6f, 0x00, 0x00, 0x08], 0); // a jump: never 0xFFFFFFFF
	const marker = new TextEncoder().encode(`HH27-STARFISH-FW ${layout} ${version}\0`);
	img.set(marker, Math.min(n - marker.length, 600));
	return img;
}

// A chip with old firmware below 0x7800 and stored items above it
function chip(oldFirmwareBytes = 20000) {
	const flash = new Uint8Array(WCHISP.FLASH_SIZE).fill(0xff);
	flash.set(image(oldFirmwareBytes, 'old0000'), 0);
	for (let i = WCHISP.STORE_START; i < flash.length; i++) flash[i] = (i * 31 + 7) & 0xff;
	return flash;
}
const storeOf = (flash) => flash.slice(WCHISP.STORE_START);

function checkWritten(flash, img, store, what) {
	bytesEq(flash.subarray(0, img.length), img, `${what}: the image`);
	const end = Math.ceil(img.length / 256) * 256;
	ok(flash.subarray(img.length, end).every((b) => b === 0xff), `${what}: the last page is padded with 0xFF`);
	bytesEq(storeOf(flash), store, `${what}: the item store`);
}

// ---- packets, compared with minichlink ----------------------------------------

// The packets minichlink's ISPSetupInterface, ISPErase and ISPHaltMode send
// (pgm-wch-isp.c), byte for byte
const MCL = {
	identify: Uint8Array.from(Buffer.from('\xa1\x12\x00\x52\x11MCU ISP & WCH.CN', 'latin1')),
	readConfig: Uint8Array.of(0xa7, 0x02, 0x00, 0x1f, 0x00),
	key: Uint8Array.from([0xa3, 0x1e, 0x00, ...new Array(30).fill(0)]),
	erase: (blobSize) => {
		const size = Math.max(8, Math.ceil(blobSize / 1024));
		return Uint8Array.of(0xa4, 0x04, 0x00, size & 0xff, size >> 8, 0, 0);
	},
	end: Uint8Array.of(0xa2, 0x01, 0x00, 0x01),
	// minichlink's XOR key: seven times the UID sum, then sum + chip id
	xorKey: (uid, chipId) => {
		const sum = uid.reduce((a, b) => (a + b) & 0xff, 0);
		return Uint8Array.of(sum, sum, sum, sum, sum, sum, sum, (sum + chipId) & 0xff);
	},
};

test('packets: the setup, erase and end packets are minichlink\'s', async () => {
	const img = image(21700);
	const rom = new MockBootRom(chip());
	await new WchIsp(rom).flashFirmware(img);
	const log = rom.log;
	bytesEq(log[0], MCL.identify, 'IDENTIFY');
	bytesEq(log[1], MCL.readConfig, 'READ_CONFIG');
	bytesEq(log[2], MCL.key, 'ISP_KEY');
	bytesEq(log[3], Uint8Array.of(0xa5, 0x05, 0x00, 0, 0, 0, 0, 0), 'the empty PROGRAM that flushes the page buffer');
	bytesEq(log[4], MCL.erase(img.length), 'ERASE');
	bytesEq(log[log.length - 1], MCL.end, 'ISP_END');
});

test('packets: PROGRAM and VERIFY data are encoded as minichlink encodes it', async () => {
	const img = image(3000);
	const rom = new MockBootRom(chip(), { chipId: 0x31, uid: [1, 2, 3, 4, 5, 6, 7, 250] });
	await new WchIsp(rom).flashFirmware(img);
	const key = MCL.xorKey(rom.uid, rom.chipId);
	const padded = new Uint8Array(Math.ceil(img.length / 256) * 256).fill(0xff);
	padded.set(img);
	let program = 0, verify = 0;
	for (const pkt of rom.log.slice(5, -1)) {
		const n = pkt[1] - 5;
		ok(pkt[0] === 0xa5 || pkt[0] === 0xa6, 'data packets are PROGRAM or VERIFY');
		eq(pkt[2], 0, 'length high byte');
		eq(pkt.length, 3 + 5 + n, 'packet length matches its length field');
		ok(n > 0 && n <= 56 && n % 8 === 0, `data length ${n}: 8..56, a multiple of 8`);
		const address = pkt[3] | (pkt[4] << 8) | (pkt[5] << 16) | (pkt[6] << 24);
		// minichlink: stream[8 + j*8 + k] = blob[i*56 + j*8 + k] ^ xor_key[k]
		const expected = padded.slice(address, address + n).map((b, i) => b ^ key[i & 7]);
		bytesEq(pkt.slice(8), expected, `data at 0x${address.toString(16)}`);
		if (pkt[0] === 0xa5) program++;
		else verify++;
	}
	eq(program, (padded.length / 256) * 5, 'PROGRAM packets: five per page');
	eq(verify, program, 'one VERIFY per PROGRAM');
});

test('key: deriveKey with the zero seed is minichlink\'s key', () => {
	for (let t = 0; t < 200; t++) {
		const uid = Uint8Array.from({ length: 8 }, rand);
		const chipId = rand() & 0x3f;
		const sum = uid.reduce((a, b) => (a + b) & 0xff, 0);
		bytesEq(WchIsp.deriveKey(new Uint8Array(30), sum, chipId), MCL.xorKey(uid, chipId), `key for uid ${hexs(uid)}`);
	}
});

// ---- the plan ---------------------------------------------------------------------

test('plan: pages 1..P-1 first, page 0 last, five packets per page', () => {
	const plan = WchIsp.plan(image(1000));
	eq(plan.pages, 4, 'pages');
	eq(plan.sectors, 8, 'at least 8 sectors are erased');
	const pages = [...new Set(plan.packets.map((p) => p.page))];
	bytesEq(pages, [1, 2, 3, 0], 'page order');
	bytesEq(plan.packets.slice(0, 5).map((p) => p.data.length), [56, 56, 56, 56, 32], 'packet sizes in a page');
	ok(plan.padded.subarray(1000).every((b) => b === 0xff), 'padding is 0xFF');
	eq(WchIsp.plan(image(9 * 1024 + 1)).sectors, 10, 'sectors round up');
});

test('plan: refuses what would touch the item store or never boot', async () => {
	eq(WchIsp.plan(image(WCHISP.STORE_START)).sectors, 30, 'an image up to the store fits');
	await rejects((async () => WchIsp.plan(image(WCHISP.STORE_START + 1)))(), 'image-too-large', 'an image into the store');
	await rejects((async () => WchIsp.plan(new Uint8Array(0)))(), 'bad-image', 'an empty image');
	const blank = image(2048);
	blank.set([0xff, 0xff, 0xff, 0xff], 0);
	await rejects((async () => WchIsp.plan(blank))(), 'bad-image', 'an image that starts with 0xFFFFFFFF');
});

test('store: the client never erases or programs at 0x7800 or above', async () => {
	await rejects((async () => new WchIsp(new MockBootRom(chip()), { limit: 0x8000 }))(), 'bad-argument', 'a limit above the store');
	const rom = new MockBootRom(chip());
	const isp = new WchIsp(rom);
	await isp.open();
	await isp.identify();
	await isp.readConfig();
	await isp.setKey();
	await rejects(isp.erase(31), 'bad-argument', 'erasing 31 sectors');
	await rejects(isp.erase(7), 'bad-argument', 'erasing fewer than 8 sectors');
	await rejects(isp.programChunk(0x7800, new Uint8Array(8)), 'bad-argument', 'programming at 0x7800');
	await rejects(isp.programChunk(0x77f8, new Uint8Array(16)), 'bad-argument', 'programming across 0x7800');
	eq(rom.log.filter((p) => p[0] === 0xa4 || p[0] === 0xa5).length, 0, 'nothing was sent');
});

// ---- whole updates on the mock ROM ------------------------------------------------

test('update: writes the image, keeps the store, page 0 last, then restarts', async () => {
	for (const n of [1024, 1000, 21700, WCHISP.STORE_START]) {
		const flash = chip();
		const store = storeOf(flash);
		const img = image(n);
		const rom = new MockBootRom(flash);
		const phases = [];
		let last = -1;
		const info = await new WchIsp(rom).flashFirmware(img, {
			onProgress: ({ phase, percent }) => {
				ok(percent >= last && percent <= 100, `progress goes up (${last} -> ${percent})`);
				last = percent;
				if (phases[phases.length - 1] !== phase) phases.push(phase);
			},
		});
		checkWritten(flash, img, store, `${n} bytes`);
		const pages = Math.ceil(n / 256);
		bytesEq(rom.programmedPages, [...Array.from({ length: pages - 1 }, (_, i) => i + 1), 0], `${n} bytes: page order`);
		ok(rom.ended, `${n} bytes: ISP end was sent`);
		eq(last, 100, `${n} bytes: progress ends at 100`);
		bytesEq(phases, ['identify', 'erase', 'write', 'verify', 'write-boot-page', 'verify-boot-page', 'reset', 'done'], `${n} bytes: phases`);
		eq(info.bytes, n, 'info.bytes');
		eq(info.bootloaderVersion, '2.60', 'info.bootloaderVersion');
		eq(info.uid, 'cd-ab-db-d5-4d-bc-f5-3d', 'info.uid');
	}
});

test('update: refuses a read-protected chip and a chip that is not a CH643', async () => {
	let flash = chip();
	const before = flash.slice();
	await rejects(new WchIsp(new MockBootRom(flash, { readProtected: true })).flashFirmware(image(2000)), 'read-protected', 'read protection');
	bytesEq(flash, before, 'a read-protected chip is not erased');
	await rejects(new WchIsp(new MockBootRom(chip(), { chipId: 0x50 })).flashFirmware(image(2000)), 'wrong-chip', 'chip id 0x50');
});

test('update: a verify mismatch stops before page 0', async () => {
	const flash = chip();
	const store = storeOf(flash);
	const rom = new MockBootRom(flash, { stuckZero: { address: 0x300, mask: 0x80 } });
	const img = image(2000);
	img[0x300] |= 0x80;
	const e = await rejects(new WchIsp(rom).flashFirmware(img), 'verify-mismatch', 'a stuck bit');
	eq(e.phase, 'verify', 'phase');
	eq(e.bootPageWritten, false, 'bootPageWritten');
	ok(flash.subarray(0, 256).every((b) => b === 0xff), 'page 0 stays erased: the ROM starts at power-on');
	bytesEq(storeOf(flash), store, 'the store');
});

// Unplug at every command of an update; then power on again and update.
async function interruptEverywhere(n) {
	const clean = new MockBootRom(chip());
	await new WchIsp(clean).flashFirmware(image(n));
	const total = clean.commands;
	for (let at = 1; at <= total; at++) {
		const flash = chip();
		const store = storeOf(flash);
		const old = flash.slice(0, WCHISP.STORE_START);
		const img = image(n, 'new1234');
		const rom = new MockBootRom(flash, { failAtCommand: at });
		let err = null;
		try {
			const info = await new WchIsp(rom).flashFirmware(img);
			eq(at, total, 'only a lost ISP end reply may end well');
			eq(info.resetReplied, false, 'the lost ISP end reply is reported');
		} catch (e) {
			err = e;
			eq(e.code, 'disconnected', `unplugged at command ${at}: ${e.message}`);
		}
		bytesEq(storeOf(flash), store, `unplugged at command ${at}: the store`);
		const erased = rom.log.some((p) => p[0] === 0xa4);
		if (!erased) bytesEq(flash.subarray(0, WCHISP.STORE_START), old, `unplugged at command ${at}: the old firmware stays`);
		else if (err && !err.bootPageWritten) ok(flash.subarray(0, 4).every((b) => b === 0xff), `unplugged at command ${at}: the first word stays erased`);
		// power on again: the ROM starts (first word erased) or the old/new
		// firmware does; either way an update from scratch must work
		const again = new MockBootRom(flash);
		await new WchIsp(again).flashFirmware(img);
		checkWritten(flash, img, store, `update after unplugging at command ${at}`);
	}
	return total;
}

test('update: unplugged at any command, nothing is lost and it can be redone (4 pages)', async () => {
	const total = await interruptEverywhere(1000);
	ok(total > 40, `${total} commands tried`);
});

test('update: unplugged at any command, nothing is lost and it can be redone (85 pages)', async () => {
	await interruptEverywhere(21700);
});

test('update: a late reply is dropped, and the same session goes on', async () => {
	for (const at of [1, 3, 5, 6, 20, 39]) { // identify, key, erase, the first PROGRAM, a later one, a VERIFY
		const flash = chip();
		const store = storeOf(flash);
		const rom = new MockBootRom(flash, { lateReplyAt: at, lateMs: 120 });
		const isp = new WchIsp(rom, { timeoutMs: 40, eraseTimeoutMs: 40, drainMs: 60 });
		const img = image(2000);
		const e = await rejects(isp.flashFirmware(img), 'timeout', `a late reply to command ${at}`);
		ok(!e.bootPageWritten, 'page 0 was not written yet');
		if (at % 2) await sleep(150); // the late reply is in before the drain ...
		// ... or arrives while the drain reads
		await isp.flashFirmware(img);
		checkWritten(flash, img, store, `after a late reply to command ${at}`);
	}
});

test('update: a half-filled page buffer from an earlier try is flushed before the erase', async () => {
	const flash = chip();
	const store = storeOf(flash);
	const rom = new MockBootRom(flash);
	const isp = new WchIsp(rom);
	await isp.open();
	await isp.identify();
	await isp.readConfig();
	await isp.setKey();
	await isp.programChunk(0x500, new Uint8Array(56)); // 56 of 256 bytes, then the try stops
	const img = image(2000);
	await isp.flashFirmware(img);
	checkWritten(flash, img, store, 'after a half page');
});

// ---- the Firmware panel ----------------------------------------------------------

// What the badge's firmware answers (webusb_protocol.c), enough for the panel
class MockBadge {
	constructor(usb, flash, { dropReplies = 0, noReplies = false } = {}) {
		this.vendorId = 0x1209;
		this.productId = 0xd643;
		this.usb = usb;
		this.flash = flash;
		this.dropReplies = dropReplies; // the first n replies after start-up get lost
		this.noReplies = noReplies;
		this.opened = false;
		this.configuration = null;
		this.gone = false;
		this.reply = null;
		this.commands = [];
	}
	// FW_INFO text in the flashed image, as the firmware's s_fw_info
	get fwInfo() {
		const text = Buffer.from(this.flash.subarray(0, WCHISP.STORE_START)).toString('latin1');
		const at = text.indexOf('HH27-STARFISH-FW ');
		return at < 0 ? null : text.slice(at, text.indexOf('\0', at));
	}
	async open() {
		if (this.gone) throw domError('NotFoundError', 'gone');
		this.opened = true;
	}
	async selectConfiguration() { this.configuration = { configurationValue: 1 }; }
	async claimInterface() {}
	async close() { this.opened = false; }
	async transferOut(ep, data) {
		if (this.gone || !this.opened) throw domError('NetworkError', 'gone');
		const cmd = data[0];
		this.commands.push(cmd);
		const status = new Uint8Array(8);
		status[4] = status[5] = 0xff;
		status[7] = 3;
		let reply = status;
		if (cmd === 0x41 && this.fwInfo !== null) { // FW_INFO, if this firmware has it
			reply = new Uint8Array(56);
			reply.set(status);
			reply.set(new TextEncoder().encode(this.fwInfo), 8);
		} else if (cmd === 0x30) { // NAME_GET
			reply = new Uint8Array(40);
			reply.set(status);
		}
		this.reply = reply;
		if (cmd === 0x40) setTimeout(() => this.usb.badgeToRom(), 3); // BOOT_ROM
		return { status: 'ok', bytesWritten: data.length };
	}
	async transferIn() {
		if (this.gone || !this.opened) throw domError('NetworkError', 'gone');
		if (this.noReplies || this.dropReplies > 0) {
			this.dropReplies--;
			throw domError('NetworkError', 'A transfer error has occurred.');
		}
		const r = this.reply;
		this.reply = null;
		return inResult(r);
	}
}

// navigator.usb with one badge on it. The boot ROM leaves after romMs if
// no host opened it; the user picks it in the chooser when it shows up for
// the pickOn-th time.
class MockUsb extends EventTarget {
	constructor(flash, { romMs = 46, pickOn = 1, badge = {} } = {}) {
		super();
		this.flash = flash;
		this.romMs = romMs;
		this.pickOn = pickOn;
		this.badgeOptions = badge;
		this.romShown = 0;
		this.chooser = null;
		this.rom = null;
		this.badge = new MockBadge(this, flash);
		this.present = new Set([this.badge]);
	}
	_fire(type, device) {
		const ev = new Event(type);
		ev.device = device;
		this.dispatchEvent(ev);
	}
	_attach(d) { this.present.add(d); this._fire('connect', d); }
	_detach(d) { d.gone = true; this.present.delete(d); this._fire('disconnect', d); }
	async getDevices() {
		return [...this.present].filter((d) => d instanceof MockBadge); // the ROM has no serial: no lasting permission
	}
	requestDevice({ filters }) {
		ok(filters.some((f) => f.vendorId === 0x1a86 && f.productId === 0x55e0), 'the chooser asks for the boot ROM');
		return new Promise((resolve) => {
			this.chooser = resolve;
			if (this.rom && this.romShown >= this.pickOn) this._pick();
		});
	}
	_pick() {
		const resolve = this.chooser;
		this.chooser = null;
		resolve(this.rom);
	}
	badgeToRom() {
		this._detach(this.badge);
		const rom = new MockBootRom(this.flash, { onGone: () => this.romGone(rom) });
		this.rom = rom;
		this.romShown++;
		this._attach(rom);
		setTimeout(() => {
			if (!rom.opened && !rom.gone) { rom.gone = true; this.romGone(rom); }
		}, this.romMs);
		if (this.chooser && this.romShown >= this.pickOn) this._pick();
	}
	romGone(rom) {
		this.rom = null;
		this.present.delete(rom);
		this._fire('disconnect', rom);
		// the badge's firmware starts again (the new one, after an update)
		setTimeout(() => {
			this.badge = new MockBadge(this, this.flash, this.badgeOptions);
			this._attach(this.badge);
		}, 8);
	}
}

// A page element with the few properties the panel uses. focus() records
// the element's id in focused, so the tests can follow the focus, and
// texts keeps every text the page wrote, as a screen reader would hear it.
function element(id, focused) {
	const listeners = {};
	const node = (name) => {
		let text = '';
		return {
			attributes: {}, texts: [],
			get textContent() { return text; },
			set textContent(value) { text = String(value); this.texts.push(text); },
			setAttribute(key, value) { this.attributes[key] = String(value); },
			focus() { focused.push(name); },
		};
	};
	return Object.assign(node(id), {
		hidden: false, disabled: false, title: '', value: 0, files: [],
		_span: node(`${id} span`),
		querySelector() { return this._span; },
		addEventListener(type, fn) { (listeners[type] ||= []).push(fn); },
		fire(type, ev = {}) { for (const fn of listeners[type] || []) fn({ target: this, ...ev }); },
	});
}

// Loads webusb.js, wchisp.js and firmware_update.js as the page does, with
// time running 100 times faster (5 s timeouts become 50 ms)
async function panel(usb, siteImage, siteCommit) {
	const els = {};
	const focused = [];
	const ctx = {
		console: { ...console, error() {} }, // the USB errors the tests cause on purpose
		TextEncoder, TextDecoder, EventTarget, Event, CustomEvent,
		setTimeout: (fn, ms, ...a) => setTimeout(fn, (ms || 0) / 100, ...a),
		clearTimeout,
		location: { hostname: 'example.org' },
		navigator: { usb },
		document: { getElementById: (id) => (els[id] ||= element(id, focused)) },
		fetch: async (url) => {
			if (url.startsWith('firmware/firmware.json')) return { ok: !!siteImage, json: async () => ({ commit: siteCommit, date: '2026-11-24' }) };
			if (url.startsWith('firmware/ledmatrix.bin')) return { ok: !!siteImage, arrayBuffer: async () => siteImage.slice().buffer };
			return { ok: false };
		},
	};
	vm.createContext(ctx);
	for (const f of ['webusb.js', 'wchisp.js']) vm.runInContext(fs.readFileSync(path.join(webui, f), 'utf8'), ctx, { filename: f });
	vm.runInContext('const device = new LedMatrixDevice();', ctx);
	vm.runInContext(fs.readFileSync(path.join(webui, 'firmware_update.js'), 'utf8'), ctx, { filename: 'firmware_update.js' });
	const run = (code) => vm.runInContext(code, ctx);
	await run('device.tryAutoReconnect()');
	return { els, run, ctx, focused };
}

async function updateFromPanel(usbOptions, what) {
	const flash = chip();
	const store = storeOf(flash);
	const usb = new MockUsb(flash, usbOptions);
	const img = image(21700, 'new5678');
	const { els, focused } = await panel(usb, img, 'new5678');
	await until(() => els['fw-badge'].textContent === 'Badge: old0000 – an update is available', `${what}: the badge's old version`);
	eq(els['fw-update-btn'].textContent, 'Update firmware to new5678', `${what}: the button names the new version`);
	eq(els['fw-update-btn'].disabled, false, `${what}: the button works`);
	eq(els['fw-confirm']._span.attributes.tabindex, '-1', `${what}: the question can take focus`);
	eq(els['fw-status'].attributes.tabindex, '-1', `${what}: the status can take focus`);
	els['fw-update-btn'].fire('click');
	ok(!els['fw-confirm'].hidden, `${what}: a confirmation is asked`);
	ok(els['fw-confirm']._span.textContent.includes('new5678'), `${what}: it names the new version`);
	eq(focused.at(-1), 'fw-confirm span', `${what}: focus moves to the question`);
	els['fw-cancel-btn'].fire('click');
	ok(els['fw-confirm'].hidden, `${what}: Cancel hides the question`);
	eq(focused.at(-1), 'fw-update-btn', `${what}: Cancel hands focus back to the button`);
	els['fw-update-btn'].fire('click');
	els['fw-status'].texts.length = 0;
	els['fw-go-btn'].fire('click');
	eq(focused.at(-1), 'fw-status', `${what}: Flash now moves focus to the status`);
	await until(() => els['fw-status'].textContent.startsWith('Done:') || els['fw-status'].textContent.startsWith('The update stopped'), `${what}: the update`);
	ok(els['fw-status'].textContent.startsWith('Done:'), `${what}: ${els['fw-status'].textContent}`);
	// The status is a live region: no more than a line per phase and 25% step
	const progress = els['fw-status'].texts.filter((t) => t.includes('%'));
	ok(progress.length >= 7 && progress.length <= 16, `${what}: ${progress.length} progress lines`);
	checkWritten(flash, img, store, what);
	await until(() => els['fw-badge'].textContent.includes('new5678') || els['fw-badge'].textContent.includes('older') || els['fw-badge'].textContent.includes('did not say'),
		`${what}: the badge's version after the update`);
	eq(els['fw-badge'].textContent, 'Badge: new5678 – up to date', `${what}: the badge's version after the update`);
	ok(els['fw-update-btn'].disabled, `${what}: an up-to-date badge needs no update`);
	ok(els['fw-status'].textContent.startsWith('Done:'), `${what}: the result stays in view`);
	return usb;
}

test('panel: an update from the page, and the badge reports the new version', async () => {
	const usb = await updateFromPanel({}, 'update');
	eq(usb.romShown, 1, 'the boot ROM came once');
});

test('panel: the page keeps the badge in the boot ROM while the chooser is open', async () => {
	const usb = await updateFromPanel({ pickOn: 3 }, 'slow pick');
	eq(usb.romShown, 3, 'the badge went into the boot ROM three times');
});

test('panel: the first replies after the restart get lost, the version still shows', async () => {
	await updateFromPanel({ badge: { dropReplies: 2 } }, 'lost replies');
});

test('panel: older firmware without FW_INFO, and a badge that does not answer', async () => {
	const flash = chip();
	flash.fill(0x13, 0, 20000); // no marker: firmware from before FW_INFO
	let { els } = await panel(new MockUsb(flash), image(21700, 'new5678'), 'new5678');
	await until(() => els['fw-badge'].textContent.includes('older'), 'older firmware');
	eq(els['fw-badge'].textContent, 'Badge: older firmware. It does not report its version.', 'older firmware');
	eq(els['fw-update-btn'].textContent, 'Update firmware to new5678', 'older firmware: the button names the new version');
	eq(els['fw-update-btn'].disabled, false, 'older firmware: the button works');

	const usb = new MockUsb(chip());
	usb.badge.noReplies = true;
	({ els } = await panel(usb, image(21700, 'new5678'), 'new5678'));
	await until(() => !els['fw-badge'].textContent.startsWith('Badge: asking'), 'no answer');
	ok(els['fw-badge'].textContent.includes('did not say'), `no answer: ${els['fw-badge'].textContent}`);
});

test('panel: an up-to-date badge says why the button is off, a chosen file turns it on', async () => {
	const { els } = await panel(new MockUsb(chip()), image(21700, 'old0000'), 'old0000');
	await until(() => els['fw-badge'].textContent.includes('up to date'), 'up to date');
	ok(els['fw-update-btn'].disabled, 'up to date: the button is off');
	eq(els['fw-status'].textContent, "The badge already runs this site's firmware.", 'up to date: the reason is on the page');
	const file = image(21700, 'mine999');
	els['fw-file-input'].fire('change', { target: { files: [{ name: 'mine.bin', arrayBuffer: async () => file.slice().buffer }], value: '' } });
	await until(() => els['fw-update-btn'].textContent === 'Flash mine.bin', 'a chosen file');
	eq(els['fw-update-btn'].disabled, false, 'a chosen file: the button works');
	eq(els['fw-status'].textContent, 'mine.bin is ready. Click "Flash mine.bin".', 'a chosen file: the status');
});

test('panel: only starfish firmware with the same item store is flashed', async () => {
	const { els, run } = await panel(new MockUsb(chip()), null, null);
	const check = (img) => {
		try { run('checkImage')(img); return 'ok'; } catch (e) { return e.message; }
	};
	eq(check(image(21700)), 'ok', 'badge firmware');
	ok(check(Uint8Array.from({ length: 21700 }, rand)).includes('no HH27-STARFISH-FW marker'), 'any other .bin');
	ok(check(image(21700, 'x', 'S2')).includes('stores items differently'), 'another store layout');
	ok(check(image(31 * 1024)).includes('the firmware region holds'), 'too large');
	ok(check(image(600)).includes('too small'), 'too small');
	eq(JSON.stringify(run('parseFirmwareId')('HH27-STARFISH-FW S1 1a2cf3a-dirty')), '{"layout":"S1","version":"1a2cf3a-dirty"}', 'parseFirmwareId');
	eq(run('parseFirmwareId')('something else'), null, 'parseFirmwareId of other text');
	ok(els['fw-update-btn'].disabled, 'without a firmware file the button is off');
});

// ---- run ------------------------------------------------------------------------

(async () => {
	let failed = 0;
	for (const t of tests) {
		const start = Date.now();
		try {
			await t.fn();
			console.log(`ok    ${t.name} (${Date.now() - start} ms)`);
		} catch (e) {
			failed++;
			console.log(`FAIL  ${t.name}\n      ${e.stack.split('\n').slice(0, 3).join('\n      ')}`);
		}
	}
	console.log(`${tests.length - failed} of ${tests.length} tests passed, ${checks} checks`);
	process.exit(failed ? 1 : 0);
})();
