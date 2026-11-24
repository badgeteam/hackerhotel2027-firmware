// A mock of the CH643 USB boot ROM (bootloader V2.60, 1a86:55e0) as a
// WebUSB USBDevice, for isp_test.js. It follows the ROM as wchisp.js
// documents it, with its own code for each rule, so the two can disagree:
//   - every command is one OUT packet and one IN reply [cmd, 0, len16, ...];
//   - the key is minichlink's (pgm-wch-isp.c ISPSetupInterface), not
//     WchIsp.deriveKey;
//   - PROGRAM fills a 256-byte page buffer and programs the page of the
//     packet that fills it; a packet under 56 bytes, or an empty one,
//     programs what is in the buffer, padded with 0xFF;
//   - programming only clears bits (flash that was not erased shows it);
//   - after a VERIFY mismatch every VERIFY answers 0xfe until the next ERASE;
//   - the IN endpoint keeps a reply until it is read, even across close()
//     and open(): a late reply answers the next read.
'use strict';

const ID_STRING = 'MCU ISP & WCH.CN';

class MockBootRom {
	// flash: Uint8Array(62 KiB), shared with the "chip" (survives sessions).
	// Options:
	//   chipId, uid (8 bytes)
	//   readProtected   READ_CONFIG reports RDPR != 0xa5
	//   failAtCommand   n: the n-th command (from 1) and all after it fail
	//                   as if the badge was unplugged
	//   lateReplyAt     n: the reply to the n-th command arrives lateMs late
	//   lateMs          default 300
	//   stuckZero       {address, mask}: these bits of that byte never program to 1
	//   onGone()        called when the ROM leaves the bus (unplug, ISP end)
	constructor(flash, options = {}) {
		this.vendorId = 0x1a86;
		this.productId = 0x55e0;
		this.flash = flash;
		this.chipId = options.chipId ?? 0x30;
		this.uid = Uint8Array.from(options.uid ?? [0xcd, 0xab, 0xdb, 0xd5, 0x4d, 0xbc, 0xf5, 0x3d]);
		this.readProtected = !!options.readProtected;
		this.failAtCommand = options.failAtCommand ?? 0;
		this.lateReplyAt = options.lateReplyAt ?? 0;
		this.lateMs = options.lateMs ?? 300;
		this.stuckZero = options.stuckZero ?? null;
		this.onGone = options.onGone ?? null;
		this.opened = false;
		this.configuration = null;
		this.gone = false;
		this.ended = false;
		this.commands = 0;
		this.log = []; // every OUT packet, as a Uint8Array
		this.programmedPages = []; // page numbers in the order they were written
		this._key = null;
		this._buf = new Uint8Array(256);
		this._count = 0;
		this._bufPage = 0;
		this._verifyFailed = false;
		this._replies = []; // the IN endpoint: replies not read yet
		this._waiting = null; // a transferIn waiting for a reply
	}

	_config() {
		return {
			configurationValue: 1,
			interfaces: [{
				interfaceNumber: 0,
				alternate: {
					endpoints: [
						{ endpointNumber: 2, direction: 'out', type: 'bulk' },
						{ endpointNumber: 2, direction: 'in', type: 'bulk' },
					],
				},
			}],
		};
	}

	_check() {
		if (this.gone) throw domError('NotFoundError', 'The device was disconnected.');
		if (!this.opened) throw domError('InvalidStateError', 'The device must be opened first.');
	}

	async open() {
		if (this.gone) throw domError('NotFoundError', 'The device was disconnected.');
		this.opened = true;
	}
	async selectConfiguration() {
		this._check();
		this.configuration = this._config();
	}
	async claimInterface() {
		this._check();
		if (!this.configuration) this.configuration = this._config();
	}
	async releaseInterface() {
		this._check();
	}
	async close() {
		this.opened = false;
		if (this._waiting) {
			this._waiting.reject(domError('AbortError', 'The transfer was cancelled.'));
			this._waiting = null;
		}
	}

	unplug() {
		if (this.gone) return;
		this.gone = true;
		this.onGone?.();
		if (this._waiting) {
			this._waiting.reject(domError('NetworkError', 'A transfer error has occurred.'));
			this._waiting = null;
		}
	}

	async transferOut(ep, data) {
		this._check();
		if (ep !== 2) throw new Error(`OUT on endpoint ${ep}`);
		const pkt = Uint8Array.from(data);
		this.commands++;
		if (this.failAtCommand && this.commands >= this.failAtCommand) {
			this.unplug();
			throw domError('NetworkError', 'A transfer error has occurred.');
		}
		this.log.push(pkt);
		const reply = this._handle(pkt);
		if (this.lateReplyAt === this.commands) setTimeout(() => this._push(reply), this.lateMs);
		else this._push(reply);
		if (this.ended) setTimeout(() => this.unplug(), 0);
		return { status: 'ok', bytesWritten: pkt.length };
	}

	transferIn(ep, length) {
		try {
			this._check();
		} catch (e) {
			return Promise.reject(e);
		}
		if (ep !== 2 || length < 64) return Promise.reject(new Error(`IN on endpoint ${ep}, ${length} bytes`));
		if (this._waiting) return Promise.reject(domError('InvalidStateError', 'A transfer is already pending.'));
		if (this._replies.length) return Promise.resolve(inResult(this._replies.shift()));
		return new Promise((resolve, reject) => {
			this._waiting = { resolve, reject };
		});
	}

	_push(reply) {
		if (this.gone) return;
		if (this._waiting) {
			const w = this._waiting;
			this._waiting = null;
			w.resolve(inResult(reply));
		} else this._replies.push(reply);
	}

	_reply(cmd, ...data) {
		const r = new Uint8Array(4 + data.length);
		r[0] = cmd;
		r[2] = data.length & 0xff;
		r[3] = data.length >> 8;
		r.set(data, 4);
		return r;
	}

	// minichlink's key: seven times the UID sum, then the sum plus the chip id
	_keyFor(seed) {
		if (seed.length < 30) return null;
		if (seed.some((b) => b !== 0)) throw new Error('the mock knows only the all-zero seed');
		const sum = this.uid.reduce((a, b) => (a + b) & 0xff, 0);
		return Uint8Array.of(sum, sum, sum, sum, sum, sum, sum, (sum + this.chipId) & 0xff);
	}

	_program(page, bytes) {
		const at = page * 256;
		for (let i = 0; i < 256; i++) this.flash[at + i] &= bytes[i];
		const s = this.stuckZero;
		if (s && s.address >= at && s.address < at + 256) this.flash[s.address] &= ~s.mask;
		this.programmedPages.push(page);
	}

	_flushBuffer() {
		if (!this._count) return;
		const padded = new Uint8Array(256).fill(0xff);
		padded.set(this._buf.subarray(0, this._count));
		this._program(this._bufPage, padded);
		this._count = 0;
	}

	_handle(pkt) {
		const cmd = pkt[0];
		const len = pkt[1] | (pkt[2] << 8);
		const payload = pkt.subarray(3, 3 + len);
		if (payload.length !== len) throw new Error(`packet ${cmd.toString(16)}: length field ${len}, ${payload.length} bytes follow`);
		switch (cmd) {
			case 0xa1: {
				const id = new TextDecoder().decode(payload.subarray(2, 18));
				return id === ID_STRING ? this._reply(cmd, this.chipId, 0x24) : this._reply(cmd, 0xf1, 0x00);
			}
			case 0xa7: {
				const cfg = new Uint8Array(26);
				cfg[2] = this.readProtected ? 0x00 : 0xa5; // RDPR, r[6]
				cfg[3] = this.readProtected ? 0xff : 0x5a;
				cfg[15] = 2; cfg[16] = 6; cfg[17] = 0; // bootloader 2.60, r[19..21]
				cfg.set(this.uid, 18); // r[22..29]
				return this._reply(cmd, ...cfg);
			}
			case 0xa3: {
				this._key = this._keyFor(payload);
				if (!this._key) return this._reply(cmd, 0xfe, 0x00);
				return this._reply(cmd, this._key.reduce((a, b) => (a + b) & 0xff, 0), 0x00);
			}
			case 0xa4: {
				const sectors = Math.min(62, Math.max(8, payload[0]));
				this.flash.fill(0xff, 0, sectors * 1024);
				this._verifyFailed = false;
				this.erasedSectors = sectors;
				return this._reply(cmd, 0x00, 0x00);
			}
			case 0xa5:
			case 0xa6: {
				if (!this._key) return this._reply(cmd, 0xfe, 0x00);
				const address = payload[0] | (payload[1] << 8) | (payload[2] << 16) | (payload[3] << 24);
				const data = payload.subarray(5).map((b, i) => b ^ this._key[i & 7]);
				if (cmd === 0xa6) {
					if (this._verifyFailed) return this._reply(cmd, 0xfe, 0x00);
					for (let i = 0; i < data.length; i++) {
						if (this.flash[address + i] !== data[i]) {
							this._verifyFailed = true;
							return this._reply(cmd, 0xf5, 0x00);
						}
					}
					return this._reply(cmd, 0x00, 0x00);
				}
				if (data.length === 0) {
					this._flushBuffer();
					return this._reply(cmd, 0x00, 0x00);
				}
				this._buf.set(data.subarray(0, 256 - this._count), this._count);
				this._count = Math.min(256, this._count + data.length);
				this._bufPage = Math.floor(address / 256);
				if (this._count === 256 || data.length < 56) this._flushBuffer();
				return this._reply(cmd, 0x00, 0x00);
			}
			case 0xa2:
				this.ended = true;
				return this._reply(cmd, 0x00, 0x00);
			default:
				return this._reply(cmd, 0xfe, 0x00);
		}
	}
}

function domError(name, message) {
	const e = new Error(message);
	e.name = name;
	return e;
}

function inResult(bytes) {
	return { status: 'ok', data: new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength) };
}

module.exports = { MockBootRom, domError, inResult };
