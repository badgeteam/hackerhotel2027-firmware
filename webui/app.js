'use strict';

const device = new LedMatrixDevice();
const board = new BoardView(document.getElementById('board-container'));

// --- Brush / painting ---------------------------------------------------

const brushValueInput = document.getElementById('brush-value');
const brushValueReadout = document.getElementById('brush-value-readout');
const brushRadiusInput = document.getElementById('brush-radius');
const liveSendInput = document.getElementById('live-send');
const eraseToggle = document.getElementById('erase-mode');

function currentBrushLevel() {
	return eraseToggle.checked ? 0 : Number(brushValueInput.value);
}

brushValueInput.addEventListener('input', () => {
	brushValueReadout.textContent = brushValueInput.value;
});

board.onPaintAt = (xmm, ymm) => {
	const radius = Number(brushRadiusInput.value);
	const level = currentBrushLevel();
	const pickRadius = Math.max(radius, 1.6); // always usable even at radius 0
	let touched = false;
	for (const led of BOARD_DATA.leds) {
		const d = Math.hypot(led.x - xmm, led.y - ymm);
		if (d <= pickRadius) {
			paintPixel(led.row, led.col, level);
			touched = true;
		}
	}
	if (touched) markFrameDirty();
};

function paintPixel(row, col, level) {
	board.setPixel(row, col, level);
	currentFrame()[row * MATRIX_COLS + col] = level;
	if (liveSendInput.checked && device.connected) {
		device.setPixel(row, col, level);
	}
}

// --- RGB LED selection/editing ---------------------------------------------------
// An RGB LED's three channels are three ordinary cells in the same frame
// array a mono LED's single cell lives in (see docs/LEDPWM_MATRIX.md) --
// so "driving" one is just three paintPixel() calls, no separate protocol
// or pattern-storage support needed.

const rgbEditor = document.getElementById('rgb-editor');
const rgbEditorLabel = document.getElementById('rgb-editor-label');
const rgbColorInput = document.getElementById('rgb-color-input');
let selectedRgbLed = null;

function rgbToHex(r, g, b) {
	return `#${[r, g, b].map((v) => v.toString(16).padStart(2, '0')).join('')}`;
}

function paintRgb(led, r, g, b) {
	board.setRgb(led, r, g, b);
	const frame = currentFrame();
	frame[led.row * MATRIX_COLS + led.col] = r;
	frame[led.green_row * MATRIX_COLS + led.col] = g;
	frame[led.blue_row * MATRIX_COLS + led.col] = b;
	if (liveSendInput.checked && device.connected) {
		device.setPixel(led.row, led.col, r);
		device.setPixel(led.green_row, led.col, g);
		device.setPixel(led.blue_row, led.col, b);
	}
	markFrameDirty();
}

board.onSelectRgb = (led) => {
	selectedRgbLed = led;
	rgbEditorLabel.textContent = led.ref;
	rgbColorInput.value = rgbToHex(...board.getRgb(led));
	rgbEditor.hidden = false;
};

rgbColorInput.addEventListener('input', () => {
	if (!selectedRgbLed) return;
	const hex = rgbColorInput.value;
	const r = parseInt(hex.slice(1, 3), 16);
	const g = parseInt(hex.slice(3, 5), 16);
	const b = parseInt(hex.slice(5, 7), 16);
	paintRgb(selectedRgbLed, r, g, b);
});

document.getElementById('rgb-editor-off-btn').addEventListener('click', () => {
	if (!selectedRgbLed) return;
	paintRgb(selectedRgbLed, 0, 0, 0);
	rgbColorInput.value = '#000000';
});

document.getElementById('rgb-editor-close-btn').addEventListener('click', () => {
	rgbEditor.hidden = true;
	selectedRgbLed = null;
});

// --- Device connection ---------------------------------------------------

const connectBtn = document.getElementById('connect-btn');
const disconnectBtn = document.getElementById('disconnect-btn');
const connStatusEl = document.getElementById('conn-status');
const deviceModeEl = document.getElementById('device-mode');

function webUsbAvailable() {
	return typeof navigator !== 'undefined' && !!navigator.usb;
}

function refreshConnButtons() {
	connectBtn.hidden = device.connected;
	disconnectBtn.hidden = !device.connected;
	connStatusEl.textContent = device.connected ? 'Connected' : 'Not connected';
	connStatusEl.classList.toggle('connected', device.connected);
}

connectBtn.addEventListener('click', async () => {
	try {
		await device.requestAndOpen();
	} catch (e) {
		if (e.name !== 'NotFoundError') alert(`Couldn't connect: ${e.message}`);
	}
});
disconnectBtn.addEventListener('click', () => device.disconnect());
device.addEventListener('connect', refreshConnButtons);
device.addEventListener('disconnect', refreshConnButtons);
device.addEventListener('status', (ev) => {
	const { mode, pattern } = ev.detail;
	deviceModeEl.textContent =
		mode === 0 ? `Built-in pattern ${pattern}` : 'Custom frame (from this page)';
});

if (!webUsbAvailable()) {
	connectBtn.disabled = true;
	connStatusEl.textContent = 'WebUSB not available in this browser';
} else {
	device.tryAutoReconnect().catch(() => {});
	setInterval(() => {
		if (device.connected) device.query();
	}, 1000);
}
refreshConnButtons();

// --- Built-in patterns / clear / send ---------------------------------------------------

document.querySelectorAll('.builtin-btn').forEach((btn) => {
	btn.addEventListener('click', () => device.selectBuiltin(Number(btn.dataset.pattern)));
});

document.getElementById('clear-btn').addEventListener('click', () => {
	board.clear();
	currentFrame().fill(0);
	if (device.connected) device.clear();
});

document.getElementById('send-frame-btn').addEventListener('click', () => {
	if (!device.connected) {
		alert('Connect to a device first.');
		return;
	}
	device.uploadFrame(Uint8Array.from(currentFrame()));
});

// --- Row/column test (bring-up debugging) ------------------------------

const testRowInput = document.getElementById('test-row-input');
const testRowReadout = document.getElementById('test-row-readout');
const testColInput = document.getElementById('test-col-input');
const testColReadout = document.getElementById('test-col-readout');

testRowInput.addEventListener('input', () => {
	testRowReadout.textContent = testRowInput.value;
});
testColInput.addEventListener('input', () => {
	testColReadout.textContent = testColInput.value;
});

// Mirrors what TEST_ROW/TEST_COL do on the device (see
// docs/WEBUSB_PROTOCOL.md) onto the local board preview + frame buffer, so
// the picture on screen matches what the matrix is actually showing.
function showTestRow(row) {
	board.clear();
	currentFrame().fill(0);
	for (let col = 0; col < MATRIX_COLS; col++) {
		board.setPixel(row, col, 255);
		currentFrame()[row * MATRIX_COLS + col] = 255;
	}
}

function showTestCol(col) {
	board.clear();
	currentFrame().fill(0);
	for (let row = 0; row < MATRIX_ROWS; row++) {
		board.setPixel(row, col, 255);
		currentFrame()[row * MATRIX_COLS + col] = 255;
	}
}

function requireDevice() {
	if (!device.connected) {
		alert('Connect to a device first.');
		return false;
	}
	return true;
}

document.getElementById('test-row-btn').addEventListener('click', () => {
	if (!requireDevice()) return;
	const row = Number(testRowInput.value);
	showTestRow(row);
	device.testRow(row);
});

document.getElementById('test-row-prev-btn').addEventListener('click', () => {
	testRowInput.value = Math.max(0, Number(testRowInput.value) - 1);
	testRowInput.dispatchEvent(new Event('input'));
	document.getElementById('test-row-btn').click();
});

document.getElementById('test-row-next-btn').addEventListener('click', () => {
	testRowInput.value = Math.min(MATRIX_ROWS - 1, Number(testRowInput.value) + 1);
	testRowInput.dispatchEvent(new Event('input'));
	document.getElementById('test-row-btn').click();
});

document.getElementById('test-col-btn').addEventListener('click', () => {
	if (!requireDevice()) return;
	const col = Number(testColInput.value);
	showTestCol(col);
	device.testCol(col);
});

document.getElementById('test-col-prev-btn').addEventListener('click', () => {
	testColInput.value = Math.max(0, Number(testColInput.value) - 1);
	testColInput.dispatchEvent(new Event('input'));
	document.getElementById('test-col-btn').click();
});

document.getElementById('test-col-next-btn').addEventListener('click', () => {
	testColInput.value = Math.min(MATRIX_COLS - 1, Number(testColInput.value) + 1);
	testColInput.dispatchEvent(new Event('input'));
	document.getElementById('test-col-btn').click();
});

document.getElementById('test-stop-btn').addEventListener('click', () => {
	board.clear();
	currentFrame().fill(0);
	if (device.connected) device.clear();
});

document.getElementById('test-pixel-btn').addEventListener('click', () => {
	if (!requireDevice()) return;
	const row = Number(document.getElementById('test-pixel-row-input').value);
	const col = Number(document.getElementById('test-pixel-col-input').value);
	board.clear();
	currentFrame().fill(0);
	board.setPixel(row, col, 255);
	currentFrame()[row * MATRIX_COLS + col] = 255;
	device.testPixel(row, col);
});

// --- Import from video ---------------------------------------------------
// Logic lives in video_import.js (extractFramesFromVideo); this just wires
// it to the panel and, on success, installs the result as a new pattern
// through the same path the pattern-list/import-JSON flows already use.

const videoFileInput = document.getElementById('video-file-input');
const videoFpsInput = document.getElementById('video-fps');
const videoStartInput = document.getElementById('video-start');
const videoDurationInput = document.getElementById('video-duration');
const videoBrightnessInput = document.getElementById('video-brightness');
const videoBrightnessReadout = document.getElementById('video-brightness-readout');
const videoAmbilightInput = document.getElementById('video-ambilight');
const videoNormalHint = document.getElementById('video-normal-hint');
const videoAmbilightHint = document.getElementById('video-ambilight-hint');
const videoConvertBtn = document.getElementById('video-convert-btn');
const videoCancelBtn = document.getElementById('video-cancel-btn');
const videoProgress = document.getElementById('video-progress');
const videoStatus = document.getElementById('video-status');

videoBrightnessInput.addEventListener('input', () => {
	videoBrightnessReadout.textContent = `${Number(videoBrightnessInput.value).toFixed(1)}x`;
});

videoAmbilightInput.addEventListener('change', () => {
	videoNormalHint.hidden = videoAmbilightInput.checked;
	videoAmbilightHint.hidden = !videoAmbilightInput.checked;
});

let videoConversionAborted = false;

videoConvertBtn.addEventListener('click', async () => {
	const file = videoFileInput.files[0];
	if (!file) {
		alert('Choose a video file first.');
		return;
	}

	videoConversionAborted = false;
	videoConvertBtn.hidden = true;
	videoCancelBtn.hidden = false;
	videoProgress.hidden = false;
	videoProgress.value = 0;
	videoStatus.textContent = 'Loading video...';

	try {
		const { frames } = await extractFramesFromVideo(
			file,
			{
				fps: Math.max(1, Math.min(20, Number(videoFpsInput.value) || 8)),
				startTime: Math.max(0, Number(videoStartInput.value) || 0),
				duration: Math.max(0.1, Number(videoDurationInput.value) || 5),
				bounds: board.bounds,
				brightnessBoost: Number(videoBrightnessInput.value) || 1,
				ambilight: videoAmbilightInput.checked,
			},
			(done, total) => {
				videoProgress.value = done / total;
				videoStatus.textContent = `Extracting frame ${done} / ${total}...`;
			},
			() => videoConversionAborted
		);

		if (videoConversionAborted) {
			videoStatus.textContent = 'Cancelled.';
			return;
		}

		const pattern = newPattern(file.name.replace(/\.[^.]+$/, ''));
		pattern.fps = Math.max(1, Math.min(20, Number(videoFpsInput.value) || 8));
		pattern.frames = frames;
		savePattern(pattern);
		renderPatternList();
		setActivePattern(pattern);

		videoStatus.textContent = `Done -- ${frames.length} frames.`;
	} catch (e) {
		videoStatus.textContent = `Failed: ${e.message}`;
	} finally {
		videoConvertBtn.hidden = false;
		videoCancelBtn.hidden = true;
		videoProgress.hidden = true;
	}
});

videoCancelBtn.addEventListener('click', () => {
	videoConversionAborted = true;
});

// --- Pattern / frame editor ---------------------------------------------------

let activePattern = newPattern('Untitled');
let activeFrameIndex = 0;
let playTimer = null;

function currentFrame() {
	return activePattern.frames[activeFrameIndex];
}

function markFrameDirty() {
	renderFrameStrip();
}

function loadFrameIntoBoard() {
	board.setFrame(currentFrame());
}

function setActivePattern(pattern) {
	stopPlayback();
	activePattern = pattern;
	activeFrameIndex = 0;
	document.getElementById('pattern-name').value = pattern.name;
	document.getElementById('pattern-fps').value = pattern.fps;
	loadFrameIntoBoard();
	renderFrameStrip();
}

document.getElementById('pattern-name').addEventListener('input', (ev) => {
	activePattern.name = ev.target.value;
});
document.getElementById('pattern-fps').addEventListener('input', (ev) => {
	activePattern.fps = Math.max(1, Math.min(20, Number(ev.target.value) || 4));
});

document.getElementById('new-pattern-btn').addEventListener('click', () => {
	setActivePattern(newPattern('Untitled'));
});

document.getElementById('save-pattern-btn').addEventListener('click', () => {
	savePattern(activePattern);
	renderPatternList();
});

document.getElementById('duplicate-pattern-btn').addEventListener('click', () => {
	const copy = JSON.parse(JSON.stringify(activePattern));
	copy.id = newPattern('x').id;
	copy.name = `${activePattern.name} copy`;
	savePattern(copy);
	setActivePattern(copy);
	renderPatternList();
});

document.getElementById('delete-pattern-btn').addEventListener('click', () => {
	if (!confirm(`Delete "${activePattern.name}"? This can't be undone.`)) return;
	deletePattern(activePattern.id);
	renderPatternList();
	setActivePattern(newPattern('Untitled'));
});

document.getElementById('export-pattern-btn').addEventListener('click', () => {
	const blob = new Blob([exportPattern(activePattern)], { type: 'application/json' });
	const a = document.createElement('a');
	a.href = URL.createObjectURL(blob);
	a.download = `${activePattern.name.replace(/[^\w-]+/g, '_') || 'pattern'}.json`;
	a.click();
	URL.revokeObjectURL(a.href);
});

document.getElementById('import-pattern-input').addEventListener('change', async (ev) => {
	const file = ev.target.files[0];
	ev.target.value = '';
	if (!file) return;
	try {
		const pattern = importPattern(await file.text());
		savePattern(pattern);
		renderPatternList();
		setActivePattern(pattern);
	} catch (e) {
		alert(`Couldn't import that file: ${e.message}`);
	}
});

function renderPatternList() {
	const list = document.getElementById('pattern-list');
	list.innerHTML = '';
	const patterns = listPatterns().sort((a, b) => b.updatedAt - a.updatedAt);
	if (patterns.length === 0) {
		list.innerHTML = '<li class="empty">No saved patterns yet.</li>';
		return;
	}
	for (const p of patterns) {
		const li = document.createElement('li');
		li.className = 'pattern-item';
		if (activePattern.id === p.id) li.classList.add('active');
		const btn = document.createElement('button');
		btn.textContent = `${p.name} (${p.frames.length} frame${p.frames.length === 1 ? '' : 's'})`;
		btn.addEventListener('click', () => {
			setActivePattern(JSON.parse(JSON.stringify(p)));
			renderPatternList();
		});
		li.appendChild(btn);
		list.appendChild(li);
	}
}

// --- Frame strip (per-pattern flipbook editing) ---------------------------------------------------

function renderFrameStrip() {
	refreshCompressionStats();
	const strip = document.getElementById('frame-strip');
	strip.innerHTML = '';
	activePattern.frames.forEach((_, i) => {
		const btn = document.createElement('button');
		btn.className = 'frame-thumb';
		btn.textContent = i + 1;
		if (i === activeFrameIndex) btn.classList.add('active');
		btn.addEventListener('click', () => {
			activeFrameIndex = i;
			loadFrameIntoBoard();
			renderFrameStrip();
		});
		strip.appendChild(btn);
	});
	document.getElementById('frame-count-label').textContent =
		`${activePattern.frames.length} frame${activePattern.frames.length === 1 ? '' : 's'}`;
}

document.getElementById('add-frame-btn').addEventListener('click', () => {
	activePattern.frames.splice(activeFrameIndex + 1, 0, currentFrame().slice());
	activeFrameIndex++;
	loadFrameIntoBoard();
	renderFrameStrip();
});

document.getElementById('delete-frame-btn').addEventListener('click', () => {
	if (activePattern.frames.length <= 1) return;
	activePattern.frames.splice(activeFrameIndex, 1);
	activeFrameIndex = Math.max(0, activeFrameIndex - 1);
	loadFrameIntoBoard();
	renderFrameStrip();
});

// --- Playback ---------------------------------------------------

const playBtn = document.getElementById('play-btn');

function stopPlayback() {
	if (playTimer) {
		clearInterval(playTimer);
		playTimer = null;
	}
	playBtn.textContent = '▶ Play';
}

function startPlayback() {
	if (activePattern.frames.length < 2) return;
	const sendToDevice = device.connected;
	playBtn.textContent = '■ Stop';
	playTimer = setInterval(async () => {
		activeFrameIndex = (activeFrameIndex + 1) % activePattern.frames.length;
		loadFrameIntoBoard();
		renderFrameStrip();
		if (sendToDevice) {
			try {
				await device.uploadFrame(Uint8Array.from(currentFrame()));
			} catch (e) {
				/* webusb.js already logs this */
			}
		}
	}, 1000 / activePattern.fps);
}

playBtn.addEventListener('click', () => {
	if (playTimer) stopPlayback();
	else startPlayback();
});

// --- Flash storage (compressed .ledanim export/import) ---------------------------------------------------
// Compression format/rationale: docs/ANIMATION_FORMAT.md, codec in
// compression.js. No firmware support for reading this back from flash
// yet (see that doc) -- this is purely the browser-side half, so there's
// a known format ready once that firmware feature exists.

const compressionQuantSelect = document.getElementById('compression-quant');
const flashBudgetInput = document.getElementById('flash-budget');
const compressionStatsEl = document.getElementById('compression-stats');

function formatBytes(n) {
	return n < 1024 ? `${n} B` : `${(n / 1024).toFixed(1)} KB`;
}

function refreshCompressionStats() {
	if (!activePattern) return;
	const quantBits = Number(compressionQuantSelect.value);
	const budgetBytes = Math.max(1, Number(flashBudgetInput.value) || 0) * 1024;
	const rawBytes = activePattern.frames.length * MATRIX_ROWS * MATRIX_COLS;
	let compressedBytes;
	try {
		compressedBytes = compressPattern(activePattern, { quantBits }).byteLength;
	} catch (e) {
		compressionStatsEl.textContent = `Can't estimate: ${e.message}`;
		return;
	}
	const pct = rawBytes ? Math.round((100 * compressedBytes) / rawBytes) : 0;
	const fits = compressedBytes <= budgetBytes;
	const overBy = compressedBytes - budgetBytes;
	compressionStatsEl.textContent = fits
		? `${activePattern.frames.length} frames: ${formatBytes(compressedBytes)} compressed ` +
		  `(${pct}% of ${formatBytes(rawBytes)} raw) -- fits the ${formatBytes(budgetBytes)} budget.`
		: `${activePattern.frames.length} frames: ${formatBytes(compressedBytes)} compressed ` +
		  `(${pct}% of ${formatBytes(rawBytes)} raw) -- ${formatBytes(overBy)} over the ` +
		  `${formatBytes(budgetBytes)} budget. Lower the brightness levels, trim frames, or drop the fps.`;
	compressionStatsEl.classList.toggle('stats-over-budget', !fits);
}

compressionQuantSelect.addEventListener('change', refreshCompressionStats);
flashBudgetInput.addEventListener('input', refreshCompressionStats);

document.getElementById('export-compressed-btn').addEventListener('click', () => {
	const quantBits = Number(compressionQuantSelect.value);
	let bytes;
	try {
		bytes = compressPattern(activePattern, { quantBits });
	} catch (e) {
		alert(`Couldn't compress this pattern: ${e.message}`);
		return;
	}
	const blob = new Blob([bytes], { type: 'application/octet-stream' });
	const a = document.createElement('a');
	a.href = URL.createObjectURL(blob);
	a.download = `${activePattern.name.replace(/[^\w-]+/g, '_') || 'pattern'}.ledanim`;
	a.click();
	URL.revokeObjectURL(a.href);
});

const uploadPatternStatusEl = document.getElementById('upload-pattern-status');

document.getElementById('upload-pattern-btn').addEventListener('click', async () => {
	if (!device.connected) {
		alert('Connect to a device first.');
		return;
	}
	const quantBits = Number(compressionQuantSelect.value);
	const budgetBytes = Math.max(1, Number(flashBudgetInput.value) || 0) * 1024;
	let bytes;
	try {
		bytes = compressPattern(activePattern, { quantBits });
	} catch (e) {
		alert(`Couldn't compress this pattern: ${e.message}`);
		return;
	}
	if (bytes.length > budgetBytes) {
		alert(
			`Compressed pattern is ${formatBytes(bytes.length)}, over the ` +
			`${formatBytes(budgetBytes)} budget -- lower the brightness levels, ` +
			`trim frames, or drop the fps first.`
		);
		return;
	}
	uploadPatternStatusEl.textContent = `Uploading ${formatBytes(bytes.length)}...`;
	try {
		await device.uploadPattern(bytes);
		uploadPatternStatusEl.textContent =
			`Stored (${formatBytes(bytes.length)}) and playing -- will autoplay on every boot from now on.`;
	} catch (e) {
		uploadPatternStatusEl.textContent = `Upload failed: ${e.message}`;
	}
});

document.getElementById('import-compressed-input').addEventListener('change', async (ev) => {
	const file = ev.target.files[0];
	ev.target.value = '';
	if (!file) return;
	try {
		const bytes = new Uint8Array(await file.arrayBuffer());
		const { fps, frames } = decompressPattern(bytes);
		const pattern = newPattern(file.name.replace(/\.[^.]+$/, ''));
		pattern.fps = fps;
		pattern.frames = frames;
		savePattern(pattern);
		renderPatternList();
		setActivePattern(pattern);
	} catch (e) {
		alert(`Couldn't load that .ledanim file: ${e.message}`);
	}
});

// --- Init ---------------------------------------------------

renderPatternList();
renderFrameStrip();
loadFrameIntoBoard();
