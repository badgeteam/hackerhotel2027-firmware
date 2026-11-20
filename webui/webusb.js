// WebUSB transport for the LED matrix protocol. See
// docs/WEBUSB_PROTOCOL.md -- this file and webusb_protocol.c must agree.
'use strict';

const USB_VENDOR_ID = 0x1209;
const USB_PRODUCT_ID = 0xd643;
const EP_OUT = 2;
const EP_IN = 1;

const CMD = {
	SET_PIXEL: 0x01,
	FRAME_CHUNK: 0x02,
	FRAME_COMMIT: 0x03,
	SELECT_BUILTIN: 0x04,
	CLEAR: 0x05,
	TEST_ROW: 0x06,
	TEST_COL: 0x07,
	DEBUG_DUMP: 0x08,
	TEST_PIXEL: 0x09,
	GPIO_TEST: 0x0a,
	RAW_TEST: 0x0b,
	SET_PWM_PIN: 0x0c,
	POKE_BRIGHTNESS: 0x0d,
	REG_WRITE32: 0x0e,
	REG_WRITE8: 0x0f,
	PATTERN_BEGIN: 0x10,
	PATTERN_CHUNK: 0x11,
	PATTERN_COMMIT: 0x12,
	QUERY: 0xff,
};

// Byte layout of the CMD_DEBUG_DUMP reply -- see led_matrix.c's
// LEDMatrix_DebugDump() and LEDMATRIX_DEBUG_DUMP_LEN. No length prefix:
// WebUSB_SendPendingReply() only ever transmits the reply body.
const DEBUG_DUMP_LEN = 16 + 4 + 4 + 2 + 3 + 8 * 4; // 61

class LedMatrixDevice extends EventTarget {
	constructor() {
		super();
		this.device = null;
		// All sends go through this chain so transferOut/transferIn calls
		// on the same endpoints never overlap.
		this._queue = Promise.resolve();
	}

	get connected() {
		return !!this.device;
	}

	async requestAndOpen() {
		const device = await navigator.usb.requestDevice({
			filters: [{ vendorId: USB_VENDOR_ID, productId: USB_PRODUCT_ID }],
		});
		await this._open(device);
	}

	// Reattaches to a device the browser already remembers permission for
	// (navigator.usb.getDevices()), without a chooser prompt. Used on page
	// load so a refresh doesn't force reconnecting by hand.
	async tryAutoReconnect() {
		const devices = await navigator.usb.getDevices();
		const match = devices.find(
			(d) => d.vendorId === USB_VENDOR_ID && d.productId === USB_PRODUCT_ID
		);
		if (match) await this._open(match);
	}

	async _open(device) {
		await device.open();
		if (device.configuration === null) await device.selectConfiguration(1);
		await device.claimInterface(0);
		this.device = device;
		// USBDevice doesn't implement EventTarget -- 'disconnect' is fired on
		// navigator.usb itself, with event.device identifying which device.
		navigator.usb.addEventListener('disconnect', this._onUsbDisconnect);
		this.dispatchEvent(new CustomEvent('connect'));
	}

	_onUsbDisconnect = (ev) => {
		if (ev.device !== this.device) return;
		navigator.usb.removeEventListener('disconnect', this._onUsbDisconnect);
		this.device = null;
		this.dispatchEvent(new CustomEvent('disconnect'));
	};

	async disconnect() {
		if (!this.device) return;
		try {
			await this.device.close();
		} catch (e) {
			/* already gone */
		}
		this.device = null;
		this.dispatchEvent(new CustomEvent('disconnect'));
	}

	// Sends one command packet and returns the 2-byte status reply as
	// {mode, pattern}, or null if nothing is connected.
	_send(bytes) {
		this._queue = this._queue.then(() => this._sendNow(bytes)).catch((e) => {
			console.error('LED matrix USB error', e);
			this.dispatchEvent(new CustomEvent('error', { detail: e }));
		});
		return this._queue;
	}

	async _sendNow(bytes) {
		if (!this.device) return null;
		await this.device.transferOut(EP_OUT, new Uint8Array(bytes));
		const result = await this.device.transferIn(EP_IN, 64);
		if (result.data && result.data.byteLength >= 2) {
			const status = { mode: result.data.getUint8(0), pattern: result.data.getUint8(1) };
			this.dispatchEvent(new CustomEvent('status', { detail: status }));
			return status;
		}
		return null;
	}

	setPixel(row, col, brightness) {
		return this._send([CMD.SET_PIXEL, row, col, brightness]);
	}

	// frame: Uint8Array(352), row-major (index = row*16+col).
	async uploadFrame(frame) {
		const CHUNK = 58;
		for (let offset = 0; offset < frame.length; offset += CHUNK) {
			const slice = frame.subarray(offset, Math.min(offset + CHUNK, frame.length));
			await this._send([
				CMD.FRAME_CHUNK,
				offset & 0xff,
				(offset >> 8) & 0xff,
				...slice,
			]);
		}
		return this._send([CMD.FRAME_COMMIT]);
	}

	selectBuiltin(patternId) {
		return this._send([CMD.SELECT_BUILTIN, patternId]);
	}

	clear() {
		return this._send([CMD.CLEAR]);
	}

	// Debug aids: isolate one row/column at full brightness so a dead
	// row/column is obvious without clicking every LED on it by hand.
	testRow(row) {
		return this._send([CMD.TEST_ROW, row]);
	}

	testCol(col) {
		return this._send([CMD.TEST_COL, col]);
	}

	// Isolates exactly one LED, unlike TEST_ROW (which can't isolate a
	// row -- see docs/LEDPWM_MATRIX.md): freezes the scan to this pixel's
	// row and statically enables only its column, so current can only
	// flow through that one intersection.
	testPixel(row, col) {
		return this._send([CMD.TEST_PIXEL, row, col]);
	}

	query() {
		return this._send([CMD.QUERY]);
	}

	// Stores a compressed .ledanim blob (webui/compression.js,
	// docs/ANIMATION_FORMAT.md) into the device's flash (pattern_flash.h)
	// and starts it playing on repeat immediately -- see
	// CMD_PATTERN_BEGIN/CHUNK/COMMIT in docs/WEBUSB_PROTOCOL.md. It then
	// autoplays on every future boot too, with no host connected.
	// PATTERN_BEGIN erases the whole reserved region (~64 page erases)
	// before replying, so this can take a couple hundred ms before the
	// first chunk should go out.
	async uploadPattern( bytes ) {
		await this._send([CMD.PATTERN_BEGIN]);
		const CHUNK = 60;
		for (let offset = 0; offset < bytes.length; offset += CHUNK) {
			const slice = bytes.subarray(offset, Math.min(offset + CHUNK, bytes.length));
			await this._send([CMD.PATTERN_CHUNK, ...slice]);
		}
		return this._send([CMD.PATTERN_COMMIT]);
	}

	// Bring-up debugging: live LEDPWM register state read back over USB
	// (see LEDMatrix_DebugDump() in led_matrix.c and CMD_DEBUG_DUMP in
	// webusb_protocol.c), instead of inferring hardware behaviour from how
	// LEDs look through a camera. Doesn't go through _send()/the generic
	// {mode,pattern} 'status' event -- the reply has a completely
	// different, longer shape. Returns the full parsed register snapshot
	// used by the manual register-control panel (regpanel.js); call
	// repeatedly to see whether values are stuck or genuinely changing.
	async debugDump() {
		if (!this.device) return null;
		this._queue = this._queue.then(() => this._debugDumpNow()).catch((e) => {
			console.error('LED matrix USB error', e);
			this.dispatchEvent(new CustomEvent('error', { detail: e }));
		});
		return this._queue;
	}

	async _debugDumpNow() {
		if (!this.device) return null;
		await this.device.transferOut(EP_OUT, new Uint8Array([CMD.DEBUG_DUMP]));
		const result = await this.device.transferIn(EP_IN, 64);
		if (!result.data || result.data.byteLength < DEBUG_DUMP_LEN) return null;
		const d = result.data;
		const bytes = [];
		for (let i = 0; i < DEBUG_DUMP_LEN; i++) bytes.push(d.getUint8(i));

		const words = [];
		for (let g = 0; g < 8; g++) words.push(d.getUint32(29 + g * 4, /* littleEndian */ true));

		const dispStat = bytes[16];
		const dump = {
			bytes,
			// SWITCH group (0x00-0x03)
			comSel: bytes[0],
			keyCtrl: bytes[1],
			pwmOe: bytes[2],
			dmaCnt: bytes[3],
			// COM_E group (0x04-0x07)
			comEPbL: bytes[4],
			comEPbM: bytes[5],
			comEPbH: bytes[6],
			comEPapc: bytes[7],
			// PWM_PIN group (0x08-0x0B)
			pwmPinL: bytes[8],
			pwmPinH: bytes[9],
			pwmPinReserved: bytes[10],
			pwmMod: bytes[11],
			// CTRL group (0x0C-0x0F)
			funcCtrl: bytes[12],
			anaCtrl: bytes[13],
			cycleCfg: bytes[14],
			frameCfg: bytes[15],
			// Status/counters (0x10-0x13)
			dispStat,
			ifInhibit: !!(dispStat & 0x80),
			inhibitCtrl: !!(dispStat & 0x10),
			intenCnt: bytes[17],
			colorCnt: bytes[18],
			frameSta: bytes[19],
			// Analog trim (0x14-0x17)
			intenAdj: bytes[20],
			redAdj: bytes[21],
			greenAdj: bytes[22],
			blueAdj: bytes[23],
			// Live DMA pointer (0x18-0x19)
			dmaAddr: bytes[24] | (bytes[25] << 8),
			// Driver scan state (not LEDPWM registers)
			lastLoadedRow: bytes[26],
			nextRow: bytes[27],
			frozenRow: bytes[28] === 0xff ? null : bytes[28],
			// Last row's packed DMA buffer, 8 words (2 columns/word)
			words: words.map((w) => '0x' + w.toString(16).padStart(8, '0')),
		};
		return dump;
	}

	// Manual register control (webui/regpanel.js): writes one atomic
	// 32-bit register group directly. group: 0=SWITCH, 1=COM_E,
	// 2=PWM_PIN, 3=CTRL. value's bytes map low-to-high onto that group's
	// four fields (see ch643_ledpwm.h). Raw poke, no validation.
	writeReg32(group, value) {
		return this._send([
			CMD.REG_WRITE32,
			group,
			value & 0xff,
			(value >> 8) & 0xff,
			(value >> 16) & 0xff,
			(value >> 24) & 0xff,
		]);
	}

	// Manual register control: writes one of the four independent analog
	// trim bytes. offset is the register's real address (0x14-0x17).
	writeReg8(offset, value) {
		return this._send([CMD.REG_WRITE8, offset, value]);
	}
}
