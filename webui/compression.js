// Binary pattern compression for flash storage. Format spec and rationale:
// docs/ANIMATION_FORMAT.md -- deliberately simple (no general-purpose
// entropy coding) so a small on-device C decoder (pattern_player.c) stays
// easy to keep byte-for-byte in sync with this file, which is the
// reference implementation.
//
// LA02 is a fully bit-packed stream (see docs/ANIMATION_FORMAT.md): once
// past the 8-byte header, nothing is byte-aligned -- frame type, run
// lengths, indices and pixel values are all packed at exactly the width
// they need, back to back, with no padding until the very end of the
// file. This matters a lot at low quant_bits (the common case for an
// actually-long animation): LA01 spent a full byte per stored value
// regardless of bit depth, which is 8x bigger than it needs to be at
// quant_bits=1.
//
// LA03 (what compressPattern() writes now) keeps the header and the
// bit-packing, and changes how frames are coded:
// - Pixels go in LAYOUT_ORDER (layout_data.js, from tools/gen_layout.py):
//   the centre grid row by row, then the outline, then the RGB LEDs.
//   Neighbours on the badge are neighbours in the stream, so runs are long.
// - Run lengths and gaps use an Elias gamma code (small numbers, few bits)
//   instead of a fixed 9 bits.
// - A shift frame moves the grid of the previous frame one LED left,
//   right, up or down before the changes are applied: scrolling costs
//   almost nothing.
// Frame types (2 bits): 0 key, 1 delta, 2 shift + delta, 3 raw. On the
// test animations (Bad Apple on the LEDs, scrolling text) LA03 comes out
// at 55-68% of LA02, and at 10-14% for text that scrolls one LED per frame.
// decompressPattern() still reads LA02.
'use strict';

const LEDANIM_MAGIC = 'LA03';
const LEDANIM_MAGIC_LA02 = 'LA02';
const LEDANIM_PIXELS = MATRIX_ROWS * MATRIX_COLS;
// Bits needed to index any single cell (0..LEDANIM_PIXELS-1) or to store
// a run length (1..LEDANIM_PIXELS, biased by -1 so it fits the same
// width) -- smallest N with 2^N >= LEDANIM_PIXELS. Recomputed from
// LEDANIM_PIXELS rather than hardcoded so this file and pattern_player.c
// agree automatically if the matrix size ever changes again -- but
// pattern_player.c's INDEX_BITS is a compile-time constant, not computed
// the same way, so a mismatch would only be caught by testing, not the
// compiler. Both currently resolve to 9 (384 pixels).
const INDEX_BITS = Math.ceil(Math.log2(LEDANIM_PIXELS));

function quantize(value, bits) {
	if (bits >= 8) return value;
	const levels = (1 << bits) - 1;
	return Math.round((value / 255) * levels);
}

function dequantize(level, bits) {
	if (bits >= 8) return level;
	const levels = (1 << bits) - 1;
	return Math.round((level / levels) * 255);
}

// --- Bit-level I/O -----------------------------------------------------
// LSB-first within each byte (the first bit written/read is bit 0 of the
// first byte). Values up to 16 bits at a time -- nothing in this format
// ever needs more (quant_bits maxes out at 8, INDEX_BITS at 9 for any
// matrix up to 32768 pixels).

class BitWriter {
	constructor() {
		this.bytes = [];
		this.acc = 0;
		this.accBits = 0;
	}
	writeBits(value, n) {
		this.acc |= (value & ((1 << n) - 1)) << this.accBits;
		this.accBits += n;
		while (this.accBits >= 8) {
			this.bytes.push(this.acc & 0xff);
			this.acc >>>= 8;
			this.accBits -= 8;
		}
	}
	// Call once at the very end -- flushes a final partial byte (zero-padded).
	finish() {
		if (this.accBits > 0) {
			this.bytes.push(this.acc & 0xff);
			this.acc = 0;
			this.accBits = 0;
		}
		return this.bytes;
	}
	get bitLength() {
		return this.bytes.length * 8 + this.accBits;
	}
}

class BitReader {
	constructor(bytes, byteOffset) {
		this.bytes = bytes;
		this.bitPos = byteOffset * 8;
	}
	readBits(n) {
		let v = 0, got = 0;
		while (got < n) {
			const byteIdx = this.bitPos >> 3;
			const bitIdx = this.bitPos & 7;
			const avail = 8 - bitIdx;
			const take = Math.min(avail, n - got);
			const chunk = (this.bytes[byteIdx] >> bitIdx) & ((1 << take) - 1);
			v |= chunk << got;
			got += take;
			this.bitPos += take;
		}
		return v;
	}
}

// --- LA02 decoding (the format before LA03) ------------------------------
// Frame type is a 2-bit field: 0=keyframe (RLE), 1=index-delta,
// 2=bitmap-delta, 3=raw; pixels in matrix order.



function decodeKeyframeBits(reader, out, quantBits) {
	let vi = 0;
	while (vi < out.length) {
		const run = reader.readBits(INDEX_BITS) + 1;
		const v = reader.readBits(quantBits);
		for (let k = 0; k < run && vi < out.length; k++) out[vi++] = v;
	}
}




function decodeIndexDeltaBits(reader, out, quantBits) {
	const count = reader.readBits(INDEX_BITS);
	for (let c = 0; c < count; c++) {
		const idx = reader.readBits(INDEX_BITS);
		const v = reader.readBits(quantBits);
		out[idx] = v;
	}
}



function decodeBitmapDeltaBits(reader, out, quantBits) {
	const changedIdx = [];
	for (let i = 0; i < LEDANIM_PIXELS; i++) if (reader.readBits(1)) changedIdx.push(i);
	for (const idx of changedIdx) out[idx] = reader.readBits(quantBits);
}



function decodeRawBits(reader, out, quantBits) {
	for (let i = 0; i < out.length; i++) out[i] = reader.readBits(quantBits);
}

// --- LA03 ----------------------------------------------------------------

// Elias gamma code for n >= 1: k zero bits, a one, then the low k bits of
// n, where k = floor(log2(n)). Written LSB-first like everything else, so
// the reader counts zeros up to the first one.
function gammaBits(n) {
	return 2 * Math.floor(Math.log2(n)) + 1;
}

function writeGamma(writer, n) {
	const k = Math.floor(Math.log2(n));
	writer.writeBits(1 << k, k + 1);
	writer.writeBits(n - (1 << k), k);
}

function readGamma(reader) {
	let k = 0;
	while (reader.readBits(1) === 0) {
		if (++k > 16) throw new Error('corrupt LA03 stream');
	}
	return (1 << k) | reader.readBits(k);
}

// The grid moved one LED: direction 0 left, 1 right, 2 up, 3 down. Pixels
// that come in from outside the grid are 0; the other LEDs stay.
const SHIFTS = [[1, 0], [-1, 0], [0, 1], [0, -1]]; // where each grid pixel takes its value from

function shiftGrid(quant, dir) {
	const [dx, dy] = SHIFTS[dir];
	const out = quant.slice();
	for (let y = 0; y < LAYOUT_GRID.length; y++) {
		for (let x = 0; x < LAYOUT_GRID[y].length; x++) {
			const dst = LAYOUT_GRID[y][x];
			if (dst === null) continue;
			const src = LAYOUT_GRID[y + dy]?.[x + dx] ?? null;
			out[dst] = src === null ? 0 : quant[src];
		}
	}
	return out;
}

// seq: values in LAYOUT_ORDER
function keyCost(seq, q) {
	let bits = 2;
	for (let i = 0; i < seq.length; ) {
		let run = 1;
		while (i + run < seq.length && seq[i + run] === seq[i]) run++;
		bits += gammaBits(run) + q;
		i += run;
	}
	return bits;
}

function encodeKey(writer, seq, q) {
	writer.writeBits(0, 2);
	for (let i = 0; i < seq.length; ) {
		let run = 1;
		while (i + run < seq.length && seq[i + run] === seq[i]) run++;
		writeGamma(writer, run);
		writer.writeBits(seq[i], q);
		i += run;
	}
}

function changedPositions(seq, prevSeq) {
	const changes = [];
	for (let i = 0; i < seq.length; i++) if (seq[i] !== prevSeq[i]) changes.push(i);
	return changes;
}

// The delta body (after the frame type, and a shift frame's direction):
// the number of changes + 1, then for each change the gap to the previous
// one (the first counted from position -1) and the new value.
function deltaBodyCost(changes, q) {
	let bits = gammaBits(changes.length + 1);
	let last = -1;
	for (const i of changes) {
		bits += gammaBits(i - last) + q;
		last = i;
	}
	return bits;
}

function encodeDeltaBody(writer, seq, changes, q) {
	writeGamma(writer, changes.length + 1);
	let last = -1;
	for (const i of changes) {
		writeGamma(writer, i - last);
		writer.writeBits(seq[i], q);
		last = i;
	}
}

function decodeDeltaBody(reader, seq, q) {
	const count = readGamma(reader) - 1;
	let pos = -1;
	for (let c = 0; c < count; c++) {
		pos += readGamma(reader);
		if (pos >= seq.length) throw new Error('corrupt LA03 stream');
		seq[pos] = reader.readBits(q);
	}
}

const inOrder = (quant) => LAYOUT_ORDER.map((slot) => quant[slot]);

// pattern: {fps, frames: [[LEDANIM_PIXELS ints], ...]}. opts.quantBits: 8
// (lossless), 4, 2, or 1.
function compressPattern(pattern, { quantBits = 8 } = {}) {
	if (pattern.frames.some((f) => f.length !== LEDANIM_PIXELS)) {
		throw new Error(`every frame must have exactly ${LEDANIM_PIXELS} values`);
	}
	const frameCount = pattern.frames.length;
	if (frameCount > 0xffff) throw new Error('too many frames (max 65535)');
	const q = quantBits;

	const header = new Uint8Array(8);
	for (let i = 0; i < 4; i++) header[i] = LEDANIM_MAGIC.charCodeAt(i);
	header[4] = frameCount & 0xff;
	header[5] = (frameCount >> 8) & 0xff;
	header[6] = Math.max(1, Math.min(255, Math.round(pattern.fps || 4)));
	header[7] = q;

	const writer = new BitWriter();
	let prev = null; // the previous frame, quantized, in slot order
	for (const frame of pattern.frames) {
		const quant = frame.map((v) => quantize(v, q));
		const seq = inOrder(quant);
		// candidates: [cost, encode()]
		const options = [
			[keyCost(seq, q), () => encodeKey(writer, seq, q)],
			[2 + LEDANIM_PIXELS * q, () => {
				writer.writeBits(3, 2);
				for (const v of seq) writer.writeBits(v, q);
			}],
		];
		if (prev) {
			const changes = changedPositions(seq, inOrder(prev));
			options.push([2 + deltaBodyCost(changes, q), () => {
				writer.writeBits(1, 2);
				encodeDeltaBody(writer, seq, changes, q);
			}]);
			for (let dir = 0; dir < SHIFTS.length; dir++) {
				const shiftedChanges = changedPositions(seq, inOrder(shiftGrid(prev, dir)));
				options.push([4 + deltaBodyCost(shiftedChanges, q), () => {
					writer.writeBits(2, 2);
					writer.writeBits(dir, 2);
					encodeDeltaBody(writer, seq, shiftedChanges, q);
				}]);
			}
		}
		options.reduce((best, o) => (o[0] < best[0] ? o : best))[1]();
		prev = quant;
	}

	const body = writer.finish();
	const out = new Uint8Array(8 + body.length);
	out.set(header, 0);
	out.set(body, 8);
	return out;
}

function decodeLa03Frames(reader, frameCount, q) {
	let quant = new Array(LEDANIM_PIXELS).fill(0); // slot order
	const frames = [];
	for (let f = 0; f < frameCount; f++) {
		const type = reader.readBits(2);
		if (type === 2) quant = shiftGrid(quant, reader.readBits(2));
		const seq = inOrder(quant);
		if (type === 0) {
			for (let i = 0; i < seq.length; ) {
				const run = readGamma(reader);
				const v = reader.readBits(q);
				for (let k = 0; k < run && i < seq.length; k++) seq[i++] = v;
			}
		} else if (type === 3) {
			for (let i = 0; i < seq.length; i++) seq[i] = reader.readBits(q);
		} else {
			decodeDeltaBody(reader, seq, q);
		}
		LAYOUT_ORDER.forEach((slot, i) => (quant[slot] = seq[i]));
		frames.push(quant.map((v) => dequantize(v, q)));
	}
	return frames;
}

function decodeLa02Frames(reader, frameCount, quantBits) {
	const quant = new Array(LEDANIM_PIXELS).fill(0);
	const frames = [];
	for (let f = 0; f < frameCount; f++) {
		const type = reader.readBits(2);
		if (type === 0) decodeKeyframeBits(reader, quant, quantBits);
		else if (type === 3) decodeRawBits(reader, quant, quantBits);
		else if (type === 1) decodeIndexDeltaBits(reader, quant, quantBits);
		else decodeBitmapDeltaBits(reader, quant, quantBits);
		frames.push(quant.map((v) => dequantize(v, quantBits)));
	}
	return frames;
}

// Reads LA03 and LA02. Returns {fps, quantBits, frames: [[LEDANIM_PIXELS ints], ...]}.
function decompressPattern(bytes) {
	const magic = String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]);
	if (magic !== LEDANIM_MAGIC && magic !== LEDANIM_MAGIC_LA02) {
		throw new Error('not a .ledanim file (bad magic bytes, or an old LA01 file -- that format is no longer supported)');
	}
	const frameCount = bytes[4] | (bytes[5] << 8);
	const fps = bytes[6];
	const quantBits = bytes[7];
	const reader = new BitReader(bytes, 8);
	const frames = magic === LEDANIM_MAGIC
		? decodeLa03Frames(reader, frameCount, quantBits)
		: decodeLa02Frames(reader, frameCount, quantBits);
	return { fps, quantBits, frames };
}
