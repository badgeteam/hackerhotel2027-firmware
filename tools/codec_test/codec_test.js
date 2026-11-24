// LA04 round trip: compresses test patterns with webui/compression.js,
// decodes them again, and checks the frames: at q = 1, 2, 4 exactly the
// quantized values; at q = 8 values with the same PWM duty (LAYOUT_GAMMA),
// which the encoder may pick instead. With an output directory it
// also writes each pattern as NAME.ledanim and the frames the decoder must
// produce as NAME.frames (raw bytes, 384 per frame), for player_test.c.
//
//   node tools/codec_test/codec_test.js [OUTDIR]
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const webui = path.join(__dirname, '..', '..', 'webui');
const ctx = { console };
vm.createContext(ctx);
for (const f of ['layout_data.js', 'compression.js']) vm.runInContext(fs.readFileSync(path.join(webui, f), 'utf8'), ctx, { filename: f });
const { compressPattern, decompressPattern, quantize, dequantize, LAYOUT_GRID, LAYOUT_ORDER, LAYOUT_GAMMA } = vm.runInContext(
	'({compressPattern, decompressPattern, quantize, dequantize, LAYOUT_GRID, LAYOUT_ORDER, LAYOUT_GAMMA})', ctx);

let seed = 1;
const rand = (n) => ((seed = (seed * 1103515245 + 12345) >>> 0) >>> 8) % n;
const blank = () => new Array(384).fill(0);

function patterns() {
	const out = {};
	out.noise = Array.from({ length: 20 }, () => Array.from({ length: 384 }, () => rand(256)));
	out.still = Array.from({ length: 5 }, () => blank());
	// a bar scrolling across the grid, one LED per frame, both ways and up/down
	const scroll = [];
	for (let t = 0; t < 40; t++) {
		const f = blank();
		LAYOUT_GRID.forEach((row, y) => row.forEach((slot, x) => {
			if (slot !== null) f[slot] = (x + y + (t < 20 ? t : 40 - t)) % 6 < 2 ? 255 : (x * y * 7) % 256;
		}));
		f[300] = t * 6; // an RGB channel changes too
		scroll.push(f);
	}
	out.scroll = scroll;
	const vert = [];
	for (let t = 0; t < 30; t++) {
		const f = blank();
		LAYOUT_GRID.forEach((row, y) => row.forEach((slot) => {
			if (slot !== null) f[slot] = (y + t) % 5 === 0 ? 200 : 0;
		}));
		vert.push(f);
	}
	out.vertical = vert;
	// a few sparkles: sparse changes
	const sparkle = [];
	let f = blank();
	for (let t = 0; t < 60; t++) {
		f = f.map((v) => (v * 3) >> 2);
		for (let k = 0; k < 4; k++) f[rand(384)] = 255;
		sparkle.push(f.slice());
	}
	out.sparkle = sparkle;
	// the outline turning, the RGB LEDs fading, a soft gradient on the grid
	const ring = [];
	for (let t = 0; t < 50; t++) {
		const f = blank();
		LAYOUT_ORDER.forEach((slot, p) => {
			if (p < 208) f[slot] = (p * 3 + t * 5) % 256;
			else if (p < 279) f[slot] = (p - 208 + t) % 71 < 10 ? 255 : 20;
			else f[slot] = Math.round(127 + 127 * Math.sin(t / 6 + p));
		});
		ring.push(f);
	}
	out.ring = ring;
	// held frames: the same frame several times in a row
	out.held = [blank(), blank(), ...Array(7).fill(sparkle[10]), ...Array(3).fill(sparkle[20]), blank()];
	return out;
}

const outDir = process.argv[2];
if (outDir) fs.mkdirSync(outDir, { recursive: true });
let failures = 0;
for (const [name, frames] of Object.entries(patterns())) {
	for (const q of [1, 2, 4, 8]) {
		const bytes = compressPattern({ fps: 10, frames }, { quantBits: q });
		const back = decompressPattern(bytes);
		const want = frames.map((fr) => fr.map((v) => dequantize(quantize(v, q), q)));
		const same = q === 8 ? (a, b) => LAYOUT_GAMMA[a] === LAYOUT_GAMMA[b] : (a, b) => a === b;
		const ok = back.frames.length === want.length && back.frames.every((fr, i) => fr.every((v, j) => same(v, want[i][j])));
		if (!ok) failures++;
		console.log(`${ok ? 'ok  ' : 'FAIL'} ${name} q=${q}: ${frames.length} frames, ${bytes.length} bytes`);
		if (outDir) {
			fs.writeFileSync(path.join(outDir, `${name}-q${q}.ledanim`), bytes);
			fs.writeFileSync(path.join(outDir, `${name}-q${q}.frames`), Buffer.from(back.frames.flat()));
		}
	}
}
process.exit(failures ? 1 : 0);
