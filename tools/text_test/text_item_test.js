// Text items, the web interface's side (webui/text_item.js): the ASCII the
// badge can show, the payload, the name. With an output directory it also
// writes payloads for emu_text_test.py, which stores them on the emulated
// badge: NAME.hht (the payload) and NAME.txt (the ASCII it must show).
//
//   node tools/text_test/text_item_test.js [OUTDIR]
'use strict';
const fs = require('fs');
const path = require('path');
const { TEXT_ITEM, textItemAscii, makeTextItem, textItemName } = require('../../webui/text_item.js');

let checks = 0, failed = 0;
function eq(a, b, what) {
	checks++;
	if (JSON.stringify(a) !== JSON.stringify(b)) {
		failed++;
		console.log(`FAIL  ${what}: got ${JSON.stringify(a)}, expected ${JSON.stringify(b)}`);
	}
}
function throws(fn, what) {
	checks++;
	try {
		fn();
	} catch (e) {
		return;
	}
	failed++;
	console.log(`FAIL  ${what}: did not throw`);
}

// What the badge can show
eq(textItemAscii('Hacker Hotel 2027'), 'Hacker Hotel 2027', 'plain ASCII stays');
eq(textItemAscii('Caf\u00e9 cr\u00e8me br\u00fbl\u00e9e, \u00c5ngstr\u00f6m'), 'Cafe creme brulee, Angstrom', 'accents are dropped');
eq(textItemAscii('\u201cquoted\u201d \u2018single\u2019 \u2013 dash\u2026'), '"quoted" \'single\' - dash...', 'quotes, dashes, ellipsis');
eq(textItemAscii('Stra\u00dfe \u00f8re \u20ac5'), 'Strasse ore EUR5', 'letters without a decomposition');
eq(textItemAscii('line one\nline two\r\n\tend'), 'line one  line two  end', 'a line break is two spaces, a tab one');
eq(textItemAscii('  lots     of   space  '), 'lots  of  space', 'runs of spaces shrink to two, the ends are trimmed');
eq(textItemAscii('\u65e5\u672c \ud83d\ude00'), '?? ?', 'what has no ASCII form shows as ?');
eq(textItemAscii('\u0000\u0007bell'), '??bell', 'control characters show as ?');
eq(textItemAscii('\ufb01ne \u2460'), 'fine 1', 'compatibility forms (fi ligature, circled 1)');

// The payload
const p = makeTextItem('Hi \u00e9!', { speed: 25, style: TEXT_ITEM.STYLE.PLAIN });
eq(Array.from(p), [0x48, 0x48, 0x54, 0x31, 25, 1, 0, 0, 0x48, 0x69, 0x20, 0x65, 0x21], 'payload bytes');
eq(Array.from(makeTextItem('x').subarray(4, 6)), [TEXT_ITEM.DEFAULT_SPEED, TEXT_ITEM.STYLE.NAMETAG], 'default speed and style');
eq(makeTextItem('x', { speed: '12' })[4], 12, 'the speed may come from an input field');
throws(() => makeTextItem('   \n '), 'no text');
throws(() => makeTextItem('x', { speed: 0 }), 'speed 0');
throws(() => makeTextItem('x', { speed: 61 }), 'speed 61');
throws(() => makeTextItem('x', { speed: 'fast' }), 'speed not a number');
throws(() => makeTextItem('x', { style: 2 }), 'unknown style');
eq(makeTextItem('a'.repeat(31000)).length, 31008, 'a long text: one byte per character');

// The name
eq(textItemName('Hello'), 'Hello', 'a short text is its own name');
eq(textItemName('Welcome to Hacker Hotel 2027, everyone'), 'Welcome to Hacker H~', 'a long text: its start');
eq(textItemName('Welcome to Hacker Hotel').length <= 20, true, 'names fit the store\'s 20 bytes');

// Payloads for the emulator test
const out = process.argv[2];
if (out) {
	const items = {
		short: ['Hi!', {}],
		plain: ['Hi!', { style: TEXT_ITEM.STYLE.PLAIN }],
		long: ['The quick brown fox jumps over the lazy dog 0123456789', { speed: 60 }],
		accents: ['H\u00e9 \u201cyo\u201d', {}],
		huge: [Array.from({ length: 4000 }, (_, i) => `w${i}`).join(' '), { speed: 60 }],
	};
	for (const [name, [text, opts]] of Object.entries(items)) {
		fs.writeFileSync(path.join(out, `${name}.hht`), makeTextItem(text, opts));
		fs.writeFileSync(path.join(out, `${name}.txt`), textItemAscii(text));
	}
}

console.log(`${failed ? 'FAIL' : 'ok  '}  text_item.js: ${checks - failed} of ${checks} checks`);
process.exit(failed ? 1 : 0);
