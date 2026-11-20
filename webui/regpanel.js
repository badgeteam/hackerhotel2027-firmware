'use strict';

// Manual LEDPWM register control panel. Reads/writes the raw peripheral
// registers directly over CMD_DEBUG_DUMP / CMD_REG_WRITE32 / CMD_REG_WRITE8
// (see webusb.js, led_matrix.c's LEDMatrix_DebugDump()/WriteReg32()/
// WriteReg8(), and ch643_ledpwm.h for where these field definitions come
// from). This is a bring-up/debugging tool: writes are raw pokes with no
// validation, same as the CMD_RAW_TEST tooling this grew out of. See
// docs/LEDPWM_MATRIX.md for the hardware background and the specific bugs
// (PWM_PIN as a column-enable *gate*, not a mask; the atomic-32-bit-write
// requirement for SWITCH/COM_E/PWM_PIN/CTRL) this panel exists to let
// someone explore directly instead of rebuilding firmware per experiment.

// --- Field metadata --------------------------------------------------------
// Each group is one atomic 32-bit register (ch643_ledpwm.h's SWITCH/COM_E/
// PWM_PIN/CTRL unions) written as a single CMD_REG_WRITE32 call -- writing
// the four bytes separately was confirmed on hardware not to reliably
// latch (see docs/LEDPWM_MATRIX.md, "Hardware bring-up: only the first
// word loads"). Each byte within a group renders as a raw hex input plus,
// where the bit layout is known, labeled checkboxes/dropdowns kept in sync
// with that hex value.

const REG_GROUPS = [
	{
		id: 0,
		name: 'SWITCH',
		addr: '0x00–0x03',
		dumpKeys: ['comSel', 'keyCtrl', 'pwmOe', 'dmaCnt'],
		desc:
			'Row-scan control. Normally rewritten every row by the scan ISR as one atomic 32-bit word (ledpwm_load_row()) — a manual write here will race the scan and typically gets overwritten within one row period unless the scan is frozen (use "Test single pixel" on the main panel first).',
		bytes: [
			{
				label: 'COM_SEL',
				desc: '[5:0] Active COM (row) pin code. See s_com_sel_for_row in led_matrix.c for the row-index → code mapping (0-15 → 32-47, 16-21 → 2-7).',
				kind: 'hex',
			},
			{
				label: 'KEY_CTRL',
				desc: 'Keyboard-scan control. Unused by this driver — always 0.',
				kind: 'hex',
			},
			{
				label: 'PWM_OE',
				desc: 'Global PWM output enable.',
				kind: 'bits',
				bits: [{ lo: 0, hi: 0, label: 'OE (output enable)' }],
			},
			{
				label: 'DMA_CNT',
				desc: '[4:0] Words left to auto-load for this row. This driver always uses 8 (MATRIX_MODE=PB24, 2 logical columns/word).',
				kind: 'hex',
			},
		],
	},
	{
		id: 1,
		name: 'COM_E',
		addr: '0x04–0x07',
		dumpKeys: ['comEPbL', 'comEPbM', 'comEPbH', 'comEPapc'],
		desc:
			'Which COM (row) pins are enabled at all. Written once at boot by LEDMatrix_Init() and not normally touched again.',
		bytes: [
			{ label: 'COM_E_PB_L', desc: 'PB[7:0] COM-enable mask.', kind: 'hex' },
			{ label: 'COM_E_PB_M', desc: 'PB[15:8] COM-enable mask.', kind: 'hex' },
			{ label: 'COM_E_PB_H', desc: 'PB[23:16] COM-enable mask.', kind: 'hex' },
			{
				label: 'COM_E_PAPC',
				desc: 'PA/PC COM-enable bits. This board’s rows are PA0-21, so PA_7_0_EN/PA_15_8_EN/PA_23_16_EN are the ones that matter.',
				kind: 'bits',
				bits: [
					{ lo: 0, hi: 0, label: 'PA_7_0_EN' },
					{ lo: 1, hi: 1, label: 'PA_15_8_EN' },
					{ lo: 2, hi: 2, label: 'PA_23_16_EN' },
					{ lo: 3, hi: 3, label: 'PC_7_0_EN' },
					{ lo: 4, hi: 4, label: 'PC_15_8_EN' },
					{ lo: 5, hi: 5, label: 'PC_20_16_EN' },
				],
			},
		],
	},
	{
		id: 2,
		name: 'PWM_PIN',
		addr: '0x08–0x0B',
		dumpKeys: ['pwmPinL', 'pwmPinH', 'pwmPinReserved', 'pwmMod'],
		desc:
			'Column (PWM) enables + matrix mode. IMPORTANT: bit0 of PWM_PIN_L/H is a master gate for the rest of that byte, not an independent per-column enable — with it clear, every other bit in the same byte stays dark no matter how many are set (see docs/LEDPWM_MATRIX.md, "PWM_PIN is a column-enable gate, not a mask"). Normally held permanently at 0xFF,0xFF by LEDMatrix_Init()/exit_pixel_test() so the per-pixel brightness data is the only thing controlling visibility.',
		bytes: [
			{
				label: 'PWM_PIN_L',
				desc: 'Column 0-7 enables, one bit per column (bit N = column N). bit0 (column 0) is the master gate for bits 1-7.',
				kind: 'colbits',
				cols: [0, 1, 2, 3, 4, 5, 6, 7],
			},
			{
				label: 'PWM_PIN_H',
				desc: 'Column 8-15 enables, one bit per column (bit N = column 8+N). bit0 (column 8) gates bits 2-7; bit1 (column 9) is independently self-sufficient and can also gate bit2 (column 10) on its own.',
				kind: 'colbits',
				cols: [8, 9, 10, 11, 12, 13, 14, 15],
			},
			{ label: 'reserved', desc: 'Unlisted in the register map.', kind: 'hex' },
			{
				label: 'PWM_MOD',
				desc: '[2:0] MATRIX_MODE (this board is wired for PB24 = 2) + per-plane PWM enables.',
				kind: 'mod',
			},
		],
	},
	{
		id: 3,
		name: 'CTRL',
		addr: '0x0C–0x0F',
		dumpKeys: ['funcCtrl', 'anaCtrl', 'cycleCfg', 'frameCfg'],
		desc:
			'Scan enable + timing. See docs/LEDPWM_MATRIX.md, "Why this register configuration", for the derivation of the values LEDMatrix_Init() currently uses.',
		bytes: [
			{
				label: 'FUNC_CTRL',
				desc: 'Enable + misc scan control.',
				kind: 'bits',
				bits: [
					{ lo: 0, hi: 0, label: 'LED_ENABLE' },
					{ lo: 1, hi: 1, label: 'INTEN_FULL (PA16I-mode only, unused here)' },
					{ lo: 2, hi: 2, label: 'PWM_OE_CLR' },
					{ lo: 3, hi: 4, label: 'COM_AHEAD', kind: 'number', max: 3 },
					{ lo: 6, hi: 6, label: 'REG_HALF (series-paired LEDs, unused here)' },
					{ lo: 7, hi: 7, label: 'IE_INHIBIT (frame IRQ enable)' },
				],
			},
			{
				label: 'ANA_CTRL',
				desc: 'Analog constant-current options. Tested earlier and found not to usefully replace the board’s series resistors — see docs/CURRENT_BUDGET.md.',
				kind: 'bits',
				bits: [
					{ lo: 4, hi: 4, label: 'PWM_REF (CC-mode enable)' },
					{ lo: 5, hi: 5, label: 'PWM_PB_CCN (column sink CC)' },
					{ lo: 6, hi: 6, label: 'PWM_PA_CCP (row source CC)' },
					{ lo: 7, hi: 7, label: 'PWM_PA_CCN (unused, PA is source-only here)' },
				],
			},
			{
				label: 'CYCLE_CFG',
				desc: 'Luminance/color PWM cycle length + reference clock divider.',
				kind: 'bits',
				bits: [
					{ lo: 0, hi: 1, label: 'CLK_FREQ (0=/1 1=/2 2=/3 3=/4)', kind: 'number', max: 3 },
					{ lo: 4, hi: 5, label: 'INTEN_CYC (0=8bit 1=7bit 2,3=6bit)', kind: 'number', max: 3 },
					{ lo: 6, hi: 6, label: 'COLOR_CYC_7BIT (0=8bit/256 1=7bit/128)' },
				],
			},
			{
				label: 'FRAME_CFG',
				desc: 'Row-switch blanking time + color-PWM repeat count.',
				kind: 'bits',
				bits: [
					{ lo: 0, hi: 2, label: 'PWM_REPEAT (N-1 repeats/row)', kind: 'number', max: 7 },
					{ lo: 4, hi: 7, label: 'INHIBIT (blanking, luminance-PWM cycles)', kind: 'number', max: 15 },
				],
			},
		],
	},
];

const TRIM_BYTES = [
	{ offset: 0x14, label: 'INTEN_ADJ', dumpKey: 'intenAdj', desc: 'Global luminance trim, signed.' },
	{ offset: 0x15, label: 'RED_ADJ', dumpKey: 'redAdj', desc: 'Global "red"-plane trim, signed.' },
	{ offset: 0x16, label: 'GREEN_ADJ', dumpKey: 'greenAdj', desc: 'Global "green"-plane trim, signed.' },
	{ offset: 0x17, label: 'BLUE_ADJ', dumpKey: 'blueAdj', desc: 'Global "blue"-plane trim (no blue plane wired on this board).' },
];

// --- DOM helpers -------------------------------------------------------

function el(tag, attrs, children) {
	const e = document.createElement(tag);
	if (attrs) for (const [k, v] of Object.entries(attrs)) {
		if (k === 'class') e.className = v;
		else if (k === 'text') e.textContent = v;
		else e.setAttribute(k, v);
	}
	if (children) for (const c of children) e.appendChild(c);
	return e;
}

function bitsToValue(bitInputs) {
	let v = 0;
	for (const { lo, hi, input, kind } of bitInputs) {
		const mask = (1 << (hi - lo + 1)) - 1;
		if (kind === 'number') {
			v |= (Number(input.value) & mask) << lo;
		} else {
			v |= (input.checked ? 1 : 0) << lo;
		}
	}
	return v;
}

function valueToBits(value, bitInputs) {
	for (const { lo, hi, input, kind } of bitInputs) {
		const mask = (1 << (hi - lo + 1)) - 1;
		const field = (value >> lo) & mask;
		if (kind === 'number') input.value = field;
		else input.checked = !!field;
	}
}

// One raw-hex byte editor: a labeled <input type=text> (2 hex digits),
// kept in sync with an optional set of bit-level controls.
function makeByteEditor(spec) {
	const hexInput = el('input', {
		type: 'text',
		maxlength: '2',
		size: '2',
		class: 'reg-hex-input',
		value: '00',
	});
	const wrap = el('div', { class: 'reg-byte' }, [
		el('div', { class: 'reg-byte-head' }, [
			el('span', { class: 'reg-byte-label', text: spec.label }),
			el('span', { text: '0x' }),
			hexInput,
		]),
		el('p', { class: 'hint reg-byte-desc', text: spec.desc }),
	]);

	let bitInputs = null;
	const syncFromHex = () => {
		const v = parseInt(hexInput.value, 16) || 0;
		if (bitInputs) valueToBits(v, bitInputs);
	};
	const syncFromBits = () => {
		const v = bitsToValue(bitInputs);
		hexInput.value = v.toString(16).padStart(2, '0');
	};
	hexInput.addEventListener('change', () => {
		hexInput.value = (parseInt(hexInput.value, 16) || 0).toString(16).padStart(2, '0');
		syncFromHex();
	});

	if (spec.kind === 'bits' || spec.kind === 'colbits') {
		const bitsRow = el('div', { class: 'reg-bits-row' });
		const defs = spec.kind === 'colbits'
			? spec.cols.map((col, i) => ({ lo: i, hi: i, label: `Col ${col}` }))
			: spec.bits;
		bitInputs = defs.map((bd) => {
			const isNumber = bd.kind === 'number';
			const input = isNumber
				? el('input', { type: 'number', min: '0', max: String(bd.max), class: 'reg-bit-number' })
				: el('input', { type: 'checkbox', class: 'reg-bit-checkbox' });
			input.addEventListener('change', syncFromBits);
			const lbl = el('label', { class: 'reg-bit-label' }, [input, document.createTextNode(' ' + bd.label)]);
			bitsRow.appendChild(lbl);
			return { lo: bd.lo, hi: bd.hi, input, kind: isNumber ? 'number' : 'checkbox' };
		});
		wrap.appendChild(bitsRow);
	} else if (spec.kind === 'mod') {
		const bitsRow = el('div', { class: 'reg-bits-row' });
		const modeSelect = el('select', { class: 'reg-bit-number' }, [
			el('option', { value: '0', text: '0: PA24+PB24' }),
			el('option', { value: '1', text: '1: PA24' }),
			el('option', { value: '2', text: '2: PB24 (this board)' }),
			el('option', { value: '3', text: '3: PB16+PA8' }),
			el('option', { value: '4', text: '4: PA15' }),
			el('option', { value: '6', text: '6: PA16I' }),
		]);
		modeSelect.addEventListener('change', syncFromBits);
		const redCb = el('input', { type: 'checkbox', class: 'reg-bit-checkbox' });
		const greenCb = el('input', { type: 'checkbox', class: 'reg-bit-checkbox' });
		const blueCb = el('input', { type: 'checkbox', class: 'reg-bit-checkbox' });
		[redCb, greenCb, blueCb].forEach((cb) => cb.addEventListener('change', syncFromBits));
		bitsRow.appendChild(el('label', { class: 'reg-bit-label' }, [
			document.createTextNode('Matrix mode '), modeSelect,
		]));
		bitsRow.appendChild(el('label', { class: 'reg-bit-label' }, [redCb, document.createTextNode(' RED_EN')]));
		bitsRow.appendChild(el('label', { class: 'reg-bit-label' }, [greenCb, document.createTextNode(' GREEN_EN')]));
		bitsRow.appendChild(el('label', { class: 'reg-bit-label' }, [blueCb, document.createTextNode(' BLUE_EN')]));
		wrap.appendChild(bitsRow);
		bitInputs = [
			{ lo: 0, hi: 2, input: modeSelect, kind: 'number' },
			{ lo: 4, hi: 4, input: redCb, kind: 'checkbox' },
			{ lo: 5, hi: 5, input: greenCb, kind: 'checkbox' },
			{ lo: 6, hi: 6, input: blueCb, kind: 'checkbox' },
		];
	}

	return {
		el: wrap,
		setValue(v) {
			hexInput.value = v.toString(16).padStart(2, '0');
			syncFromHex();
		},
		getValue() {
			return parseInt(hexInput.value, 16) || 0;
		},
	};
}

// --- Panel construction --------------------------------------------------

function buildGroupPanel(group) {
	const byteEditors = group.bytes.map(makeByteEditor);
	const writeBtn = el('button', { class: 'reg-write-btn', text: `Write ${group.name}` });
	const status = el('span', { class: 'reg-write-status' });
	writeBtn.addEventListener('click', async () => {
		const value =
			byteEditors[0].getValue() |
			(byteEditors[1].getValue() << 8) |
			(byteEditors[2].getValue() << 16) |
			(byteEditors[3].getValue() << 24);
		writeBtn.disabled = true;
		status.textContent = 'writing…';
		try {
			await device.writeReg32(group.id, value >>> 0);
			status.textContent = `wrote 0x${(value >>> 0).toString(16).padStart(8, '0')}`;
		} catch (e) {
			status.textContent = `error: ${e.message}`;
		} finally {
			writeBtn.disabled = false;
		}
	});

	const panel = el('div', { class: 'reg-group' }, [
		el('div', { class: 'reg-group-head' }, [
			el('strong', { text: `${group.name} ` }),
			el('span', { class: 'reg-group-addr', text: group.addr }),
		]),
		el('p', { class: 'hint', text: group.desc }),
		...byteEditors.map((be) => be.el),
		el('div', { class: 'button-row' }, [writeBtn, status]),
	]);

	return { panel, byteEditors, group };
}

function buildTrimPanel(spec) {
	const input = el('input', { type: 'number', min: '-128', max: '127', value: '0', class: 'reg-bit-number' });
	const writeBtn = el('button', { text: `Write ${spec.label}` });
	const status = el('span', { class: 'reg-write-status' });
	writeBtn.addEventListener('click', async () => {
		const signed = Math.max(-128, Math.min(127, Number(input.value) | 0));
		const unsigned = signed < 0 ? signed + 256 : signed;
		writeBtn.disabled = true;
		status.textContent = 'writing…';
		try {
			await device.writeReg8(spec.offset, unsigned);
			status.textContent = `wrote ${signed} (0x${unsigned.toString(16).padStart(2, '0')})`;
		} catch (e) {
			status.textContent = `error: ${e.message}`;
		} finally {
			writeBtn.disabled = false;
		}
	});
	const panel = el('div', { class: 'reg-byte' }, [
		el('div', { class: 'reg-byte-head' }, [
			el('span', { class: 'reg-byte-label', text: `${spec.label} (0x${spec.offset.toString(16)})` }),
			input,
		]),
		el('p', { class: 'hint reg-byte-desc', text: spec.desc }),
		el('div', { class: 'button-row' }, [writeBtn, status]),
	]);
	return { panel, input, spec };
}

function buildReadonlyPanel() {
	const rows = [
		['DISP_STAT', 'dispStat'],
		['  IF_INHIBIT (frame IRQ flag)', 'ifInhibit'],
		['  INHIBIT_CTRL (safe to reload now)', 'inhibitCtrl'],
		['INTEN_CNT', 'intenCnt'],
		['COLOR_CNT', 'colorCnt'],
		['FRAME_STA', 'frameSta'],
		['DMA pointer (live, SRAM addr)', 'dmaAddr'],
		['last_loaded_row (driver state)', 'lastLoadedRow'],
		['next_row (driver state)', 'nextRow'],
		['frozen_row (driver state, blank = not frozen)', 'frozenRow'],
	];
	const tbody = el('tbody');
	const cells = {};
	for (const [label, key] of rows) {
		const valueCell = el('td');
		cells[key] = valueCell;
		tbody.appendChild(el('tr', {}, [el('td', { text: label }), valueCell]));
	}
	const wordsList = el('ul', { class: 'reg-dma-words' });
	const panel = el('div', {}, [
		el('p', { class: 'hint', text: 'Read-only status/counters + this driver’s own scan-state bytes (not writable registers).' }),
		el('table', { class: 'reg-readonly-table' }, [tbody]),
		el('p', { class: 'hint', text: 'Last row packed into the DMA SRAM buffer (luminance, red, green, blue per 2-column group):' }),
		wordsList,
	]);
	return {
		panel,
		update(dump) {
			for (const [, key] of rows) {
				let v = dump[key];
				if (typeof v === 'boolean') v = v ? 'yes' : 'no';
				if (v === null) v = '— (not frozen)';
				else if (typeof v === 'number' && key !== 'dmaAddr') v = `${v} (0x${v.toString(16)})`;
				else if (key === 'dmaAddr') v = `0x${v.toString(16).padStart(4, '0')}`;
				cells[key].textContent = v;
			}
			wordsList.innerHTML = '';
			dump.words.forEach((w, g) => {
				const n = parseInt(w, 16);
				const lum = n & 0xff, red = (n >> 8) & 0xff, green = (n >> 16) & 0xff, blue = (n >> 24) & 0xff;
				wordsList.appendChild(el('li', {
					text: `group ${g}: col${g}(red)=${red} col${g + 8}(green)=${green} lum=${lum} blue=${blue}`,
				}));
			});
		},
	};
}

// --- Wire-up ---------------------------------------------------------------

function initRegPanel() {
	const container = document.getElementById('regpanel-container');
	if (!container) return;

	const groupPanels = REG_GROUPS.map(buildGroupPanel);
	const trimPanels = TRIM_BYTES.map(buildTrimPanel);
	const readonly = buildReadonlyPanel();

	const groupsRow = el('div', { class: 'reg-groups-row' }, groupPanels.map((g) => g.panel));
	const trimRow = el('div', { class: 'reg-groups-row' }, trimPanels.map((t) => t.panel));

	const refreshBtn = el('button', { text: 'Refresh from device' });
	const autoRefresh = el('input', { type: 'checkbox' });
	const refreshStatus = el('span', { class: 'reg-write-status' });

	async function refresh() {
		if (!device.connected) {
			refreshStatus.textContent = 'not connected';
			return;
		}
		const dump = await device.debugDump();
		if (!dump) {
			refreshStatus.textContent = 'no reply';
			return;
		}
		for (const { byteEditors, group } of groupPanels) {
			group.dumpKeys.forEach((key, i) => byteEditors[i].setValue(dump[key]));
		}
		for (const { input, spec } of trimPanels) {
			const raw = dump[spec.dumpKey];
			input.value = raw > 127 ? raw - 256 : raw;
		}
		readonly.update(dump);
		refreshStatus.textContent = `updated ${new Date().toLocaleTimeString()}`;
	}
	refreshBtn.addEventListener('click', refresh);

	let timer = null;
	autoRefresh.addEventListener('change', () => {
		if (timer) { clearInterval(timer); timer = null; }
		if (autoRefresh.checked) timer = setInterval(refresh, 500);
	});

	container.appendChild(el('div', { class: 'button-row' }, [
		refreshBtn,
		el('label', {}, [autoRefresh, document.createTextNode(' Auto-refresh (2/s)')]),
		refreshStatus,
	]));
	container.appendChild(el('p', { class: 'hint', text: readonly.panel.querySelector ? '' : '' }));
	container.appendChild(readonly.panel);
	container.appendChild(el('h3', { text: 'Atomic register groups' }));
	container.appendChild(el('p', { class: 'hint', text: 'Each group below is one combined 32-bit register on real silicon (see ch643_ledpwm.h) — writing it writes all four bytes atomically in one USB command, matching how the firmware itself must write these to get a reliable result.' }));
	container.appendChild(groupsRow);
	container.appendChild(el('h3', { text: 'Analog trim bytes' }));
	container.appendChild(el('p', { class: 'hint', text: 'Independent bytes, each written on its own (no atomicity requirement).' }));
	container.appendChild(trimRow);

	device.addEventListener('connect', refresh);
	if (device.connected) refresh();
}

if (document.readyState === 'loading') {
	document.addEventListener('DOMContentLoaded', initRegPanel);
} else {
	initRegPanel();
}
