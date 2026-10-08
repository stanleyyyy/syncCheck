// Sync Analyzer web UI.
import { api, subscribe } from './api.js';
import { formatBytes, formatMs, formatTime, pairLabel, quality, slope, el } from './format.js';
import { initTheme, onThemeChange, seriesColor } from './theme.js';
import { Timeline } from './timeline.js';
import { Inspector } from './inspector.js';
import { SettingsDrawer } from './settings.js';
import { SignalDialog } from './signal.js';
import { SessionsPanel } from './sessions.js';

const $ = (id) => document.getElementById(id);

const app = {
	state: null,
	sessionId: 0,
	pairs: [],
	store: { t: [], pairs: [] },
	labels: [],
	selection: null,	// selected session time, null = live / latest
	inspectTimer: null,
	inspectSeq: 0,
	renderedSeq: 0,
	audio: null,		// playing Audio element
};

const timeline = new Timeline({
	delayEl: $('delayChart'), metricEl: $('metricChart'), overviewEl: $('overviewChart'),
	onSelect: (time) => select(time),
	onFollowChange: (follow) => { $('follow').checked = follow; updateRangeButtons(); updateCards(); },
});
const inspector = new Inspector({
	corrEl: $('corrChart'), waveEl: $('waveChart'), overviewEl: $('waveOverview'), keysEl: $('waveKeys'), tableEl: $('inspectTable'), noAudioEl: $('noAudio'),
	corrReadout: $('corrReadout'), waveReadout: $('waveReadout'),
	// immediate: while dragging (already throttled by the inspector)
	onViewChange: (immediate) => (immediate ? inspect() : scheduleInspect(150, true)),
});
const settingsDrawer = new SettingsDrawer({ drawer: $('settingsDrawer'), scrim: $('scrim'), body: $('settingsBody'),
	onSaved: (settings) => { app.state.settings = settings; renderHeader(); } });
const sessions = new SessionsPanel({ list: $('sessionList'), search: $('sessionSearch'), count: $('sessionCount'), footer: $('sessionsDisk'),
	selectionBar: $('selectionBar'), selectButton: $('selectSessions'),
	onOpened: (state) => { onState(state); if (phone.matches) setSidebar(false); }, onError: showError });

// The session list collapses to a narrow rail; the choice is remembered, narrow windows start collapsed.
// On phones it slides in over the page and closes again after use.
const phone = matchMedia('(max-width: 760px)');
function setSidebar(show, remember = true) {
	$('appBody').classList.toggle('collapsed', !show);
	$('toggleSidebar').title = show ? 'Collapse the session list' : 'Show the session list';
	if (remember && !phone.matches) {
		try { localStorage.setItem('sidebar', show ? '1' : '0'); } catch (e) { /* storage unavailable */ }
	}
}
let sidebarStored = null;
try { sidebarStored = localStorage.getItem('sidebar'); } catch (e) { /* storage unavailable */ }
setSidebar(window.innerWidth >= 1100 && sidebarStored !== '0', false);
phone.addEventListener('change', (e) => { if (e.matches) setSidebar(false, false); });
$('toggleSidebar').addEventListener('click', () => setSidebar($('appBody').classList.contains('collapsed')));
// Animations on from the next frame, after the initial sidebar state is in place.
requestAnimationFrame(() => requestAnimationFrame(() => document.body.classList.remove('loading')));
$('openSidebarPhone').addEventListener('click', () => setSidebar(true));
$('sidebarScrim').addEventListener('click', () => setSidebar(false));

// Phone menu: the top bar actions that do not fit.
$('phoneMenuButton').addEventListener('click', (e) => { e.stopPropagation(); $('phoneMenu').classList.toggle('hidden'); });
$('phoneMenu').addEventListener('click', (e) => {
	const target = e.target.dataset.click;
	$('phoneMenu').classList.add('hidden');
	if (target && !$(target).classList.contains('disabled')) $(target).click();
});
document.addEventListener('click', () => $('phoneMenu').classList.add('hidden'));
const signalDialog = new SignalDialog({ dialog: $('signalDialog'), body: $('signalBody'), download: $('downloadSignal') });

function showError(message) {
	$('errorBanner').textContent = message || '';
	$('errorBanner').classList.toggle('hidden', !message);
}

function session() {
	return app.state ? app.state.session : null;
}

// Header: input summary, status, start/stop.
function renderHeader() {
	const state = app.state;
	if (!state) return;
	const s = state.settings;
	const current = session();
	let source;
	if (current && (state.running || current.duration)) {
		source = `${current.input} · ${current.channels} ch · ${current.sampleRate / 1000} kHz`;
	} else {
		source = s.source === 'file' ? (s.file.split(/[\\/]/).pop() || 'No file selected') : `${s.device} · ${s.channels} ch · ${s.sampleRate / 1000} kHz`;
	}
	source += s.mode === 'channels' ? ' · comparing channels' : ' · comparing bands';
	$('source').textContent = source;
	$('emptySetup').textContent = `Current input: ${source}`;

	const error = state.status.startsWith('Error');
	$('status').dataset.state = state.running ? 'running' : error ? 'error' : 'idle';
	$('statusText').textContent = error ? 'Error' : state.status;
	showError(error ? state.status.replace(/^Error: /, '') : '');

	const button = $('startStop');
	button.textContent = state.running ? 'Stop' : 'Start';
	button.classList.toggle('stop', state.running);
	const hasAudio = !!(current && current.audio);
	$('exportAudio').classList.toggle('disabled', !hasAudio);
	$('exportCsv').classList.toggle('disabled', !app.store.t.length);
	// Playback is for reviewing a finished session.
	$('playback').classList.toggle('hidden', state.running || !hasAudio);
	if (state.running) stopPlayback();
}

// Pair cards: values of the selected moment (or the latest), quality and drift over the visible range.
function updateCards() {
	const container = $('pairs');
	const store = app.store;
	if (!app.labels.length) {
		// Session started, pairs not known yet: a placeholder of the same size.
		container.replaceChildren(...(app.sessionId ? [el('div', { class: 'card muted', text: 'Waiting for the first measurement…' })] : []));
		return;
	}
	const index = app.selection !== null ? nearestIndex(app.selection) : store.t.length - 1;
	const [from, to] = timeline.visibleIndexRange();

	container.replaceChildren(...app.labels.map((label, i) => {
		const pair = store.pairs[i];
		const has = pair && index >= 0 && index < pair.delay.length;
		const delay = has ? pair.delay[index] : NaN;
		const confidence = has ? pair.confidence[index] : 0;
		const pslr = has ? pair.pslr[index] : 0;
		const q = quality(pslr, confidence);
		const drift = pair ? slope(store.t, pair.delay, from, to) * 60 : NaN;
		return el('div', { class: 'card' },
			el('div', { class: 'label' }, el('span', { class: 'key', style: `background:${seriesColor(i)}` }), label),
			el('div', { class: 'value' }, has ? formatMs(delay) : '–', el('small', { text: 'ms' })),
			// Same lines before and after the first values, so the card keeps its size.
			el('div', { class: 'meta' },
				el('div', { text: has ? `${(confidence * 100).toFixed(0)} % · PSLR ${pslr.toFixed(1)} dB` : '– % · PSLR – dB' }),
				el('div', { text: `drift ${Number.isFinite(drift) ? formatMs(drift, 3) : '–'} ms/min in view` })),
			el('span', { class: 'quality', 'data-q': has ? q.key : 'none' }, el('span', { class: 'icon' }), has ? q.label : '–'));
	}));
}

function nearestIndex(time) {
	const t = app.store.t;
	if (!t.length) return -1;
	let lo = 0, hi = t.length - 1;
	while (lo < hi) {
		const mid = (lo + hi) >> 1;
		if (t[mid] < time) lo = mid + 1; else hi = mid;
	}
	return lo > 0 && Math.abs(t[lo - 1] - time) < Math.abs(t[lo] - time) ? lo - 1 : lo;
}

// Recording: file size, write speed, free space and how long it lasts.
function onDisk(disk) {
	const chip = $('disk');
	const recording = app.state && app.state.running && disk.size !== undefined;
	chip.classList.toggle('hidden', !recording);
	if (!recording) return;
	const parts = [`${formatBytes(disk.size)}`];
	if (disk.rate) parts.push(`${formatBytes(disk.rate)}/s`);
	if (disk.free !== undefined) {
		parts.push(`${formatBytes(disk.free)} free` + (disk.remaining ? ` (~${formatTime(disk.remaining)})` : ''));
	}
	chip.textContent = parts.join(' · ');
	chip.classList.toggle('low', disk.remaining !== undefined && disk.remaining < 600);
}

function updateElapsed() {
	const t = app.store.t;
	$('elapsed').textContent = t.length ? formatTime(t[t.length - 1]) : '';
}

function updateRangeButtons() {
	for (const button of $('rangeButtons').querySelectorAll('button')) {
		button.classList.toggle('on', timeline.follow && Number(button.dataset.range) === timeline.rangeSeconds);
	}
}

// The layout is shown as soon as there is a session, so it does not jump when the first data arrives.
function updateVisibility() {
	const hasData = app.sessionId > 0;
	$('empty').classList.toggle('hidden', hasData);
	$('pairs').classList.toggle('hidden', !hasData);
	$('workspace').classList.toggle('hidden', !hasData);
}

// What playback can play: all channels, or a pair as stereo (reference left).
function updateListenOptions(current) {
	const options = [el('option', { value: '', text: 'All channels' })];
	if (current && current.mode === 'channels') {
		current.pairs.forEach((p) => options.push(el('option', { value: `${p.ref},${p.sig}`,
			text: `Ch ${p.ref + 1} left + Ch ${p.sig + 1} right` })));
	}
	$('listen').replaceChildren(...options);
}

// A new session: reset the data and rebuild the charts.
function startSession(current) {
	stopPlayback();
	app.sessionId = current ? current.id : 0;
	app.pairs = current ? current.pairs : [];
	app.labels = app.pairs.map((p) => pairLabel(p, current.mode));
	app.selection = null;
	app.store = { t: [], pairs: app.labels.map(() => ({ delay: [], confidence: [], pslr: [], refRms: [], sigRms: [] })) };
	updateVisibility();
	timeline.setPairs(app.labels);
	timeline.setData(app.store);
	inspector.setPairs(app.labels, current && current.sampleRate ? current.blockSize / 2 / current.sampleRate * 1000 : 0);
	updateListenOptions(current);
	updateCards();
	updateInspectorHeader();
}

function onState(state) {
	const previous = app.state;
	app.state = state;
	const current = state.session;
	if (!current) {
		if (app.sessionId) startSession(null);
	} else if (current.id !== app.sessionId || (current.pairs.length && current.pairs.length !== app.labels.length)) {
		startSession(current);
	}
	renderHeader();
	updateInspectorHeader();
	if (!state.running) $('disk').classList.add('hidden');
	sessions.setRunning(state.running);
	if (!previous || previous.running !== state.running || !previous.session !== !state.session
		|| (previous.session && state.session && (previous.session.id !== state.session.id || previous.session.name !== state.session.name))) {
		sessions.refresh();
	}
	if (previous && previous.running && !state.running) scheduleInspect(0);
}

function onMeasurements(batch) {
	if (batch.session !== app.sessionId) return;
	const store = app.store;
	if (batch.from === 0 && store.t.length) {
		store.t = [];
		store.pairs.forEach((p) => Object.keys(p).forEach((k) => { p[k] = []; }));
	}
	if (batch.from !== store.t.length) return;
	store.t.push(...batch.t);
	batch.pairs.forEach((p, i) => {
		const target = store.pairs[i];
		if (!target) return;
		for (const key of Object.keys(target)) target[key].push(...p[key]);
	});
	timeline.setData(store);
	updateCards();
	updateElapsed();
	renderHeader();
	if (app.selection === null) scheduleInspect(500, false);
}

// Inspector: the selected moment or, without a selection, the latest measurement.
function updateInspectorHeader() {
	const live = app.selection === null;
	const running = app.state && app.state.running;
	$('inspectTime').textContent = live ? (running ? '' : '· last measurement') : `· ${formatTime(app.selection, true)}`;
	$('liveBadge').classList.toggle('hidden', !(live && running));
	$('backToLive').classList.toggle('hidden', live);
	$('backToLive').textContent = running ? 'Back to live' : 'Show latest';
}

// restart: wait until requests stop coming (zooming); otherwise refresh at most once per delay (live updates).
function scheduleInspect(delay, restart = true) {
	if (app.inspectTimer) {
		if (!restart) return;
		clearTimeout(app.inspectTimer);
	}
	app.inspectTimer = setTimeout(() => { app.inspectTimer = null; inspect(); }, delay);
}

async function inspect() {
	if (!app.sessionId || !app.store.t.length) return;
	const seq = ++app.inspectSeq;
	const params = inspector.params();
	if (app.selection === null) params.live = 1; else params.t = app.selection;
	try {
		const result = await api.inspect(params);
		// Show any answer newer than the one shown; while dragging several requests overlap.
		if (seq <= app.renderedSeq) return;
		app.renderedSeq = seq;
		$('inspectError').classList.add('hidden');
		inspector.render(result);
	} catch (e) {
		if (seq !== app.inspectSeq) return;
		$('inspectError').textContent = e.message;
		$('inspectError').classList.remove('hidden');
	}
}

function select(time) {
	app.selection = time;
	timeline.setSelection(time);
	updateInspectorHeader();
	updateCards();
	inspect();
}

function backToLive() {
	app.selection = null;
	timeline.setSelection(null);
	updateInspectorHeader();
	updateCards();
	inspect();
}

// Playback of the session audio from the selected moment (or the start of the view) to the end of the view.
function startPlayback() {
	const current = session();
	if (!current || !current.audio) return;
	const range = timeline.currentRange();
	const from = app.selection !== null ? app.selection : range.min;
	let to = range.max;
	if (to - from < 1) to = from + 30;
	const params = new URLSearchParams({ from: from.toFixed(3), to: to.toFixed(3), channels: $('listen').value });
	const audio = new Audio(`api/audio.wav?${params}`);
	app.audio = audio;
	$('playButton').textContent = '■ Stop';
	const tick = () => {
		if (app.audio !== audio) return;
		timeline.setPlayhead(from + audio.currentTime);
		requestAnimationFrame(tick);
	};
	audio.addEventListener('playing', () => requestAnimationFrame(tick));
	audio.addEventListener('ended', stopPlayback);
	audio.addEventListener('error', () => { showError('Cannot play the session audio'); stopPlayback(); });
	audio.play().catch(() => stopPlayback());
}

function stopPlayback() {
	if (app.audio) {
		app.audio.pause();
		app.audio = null;
	}
	$('playButton').textContent = '▶ Play';
	timeline.setPlayhead(null);
}

// Keyboard: ← → step, Shift for 10 steps, + − zoom, Home/End, Space play, Esc back to live.
document.addEventListener('keydown', (e) => {
	if (e.target.closest('input, select, textarea, dialog') || !app.store.t.length) return;
	const t = app.store.t;
	if (e.key === 'Escape') {
		if (!$('settingsDrawer').classList.contains('hidden')) settingsDrawer.close(); else backToLive();
		return;
	}
	if (e.key === ' ') {
		if (app.audio) stopPlayback(); else if (!app.state.running) startPlayback();
	} else if (e.key === '+' || e.key === '=') {
		timeline.zoom(1 / 1.5, app.selection);
	} else if (e.key === '-' || e.key === '_') {
		timeline.zoom(1.5, app.selection);
	} else if (e.key === 'Home' || e.key === 'End') {
		select(e.key === 'Home' ? t[0] : t[t.length - 1]);
		const range = timeline.currentRange();
		const width = range.max - range.min;
		timeline.setXRange(app.selection - width / 2, app.selection + width / 2);
	} else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
		const step = (e.key === 'ArrowLeft' ? -1 : 1) * (e.shiftKey ? 10 : 1);
		const current = app.selection === null ? t.length - 1 : nearestIndex(app.selection);
		const time = t[Math.max(0, Math.min(t.length - 1, current + step))];
		// Keep the selection in view.
		const range = timeline.currentRange();
		if (time < range.min || time > range.max) {
			const width = range.max - range.min;
			timeline.setXRange(time - width / 2, time + width / 2);
		}
		select(time);
	} else {
		return;
	}
	e.preventDefault();
});

// Controls
$('startStop').addEventListener('click', async () => {
	const button = $('startStop');
	button.disabled = true;
	try {
		app.state = app.state.running ? await api.stop() : await api.start();
		onState(app.state);
	} catch (e) {
		showError(e.message);
	} finally {
		button.disabled = false;
	}
});
const openSettings = () => settingsDrawer.open(app.state.settings, app.state.running);
$('openSettings').addEventListener('click', openSettings);
$('emptySettings').addEventListener('click', openSettings);
$('closeSettings').addEventListener('click', () => settingsDrawer.close());
$('cancelSettings').addEventListener('click', () => settingsDrawer.close());
$('resetSettings').addEventListener('click', () => settingsDrawer.reset());
$('scrim').addEventListener('click', () => settingsDrawer.close());
$('saveSettings').addEventListener('click', () => settingsDrawer.save());
$('openSignal').addEventListener('click', () => {
	const current = session();
	signalDialog.open({ ...app.state.settings, channels: current && current.channels ? current.channels : app.state.settings.channels });
});
$('closeSignal').addEventListener('click', () => $('signalDialog').close());
$('exportButton').addEventListener('click', (e) => { e.stopPropagation(); $('exportMenu').classList.toggle('hidden'); });
document.addEventListener('click', () => $('exportMenu').classList.add('hidden'));
$('follow').addEventListener('change', (e) => { timeline.setFollow(e.target.checked); updateRangeButtons(); updateCards(); });
$('rangeButtons').addEventListener('click', (e) => {
	const range = e.target.dataset.range;
	if (range === undefined) return;
	timeline.setRangeSeconds(Number(range));
	updateRangeButtons();
	updateCards();
});
$('metricButtons').addEventListener('click', (e) => {
	const metric = e.target.dataset.metric;
	if (!metric) return;
	timeline.setMetric(metric);
	timeline.setSelection(app.selection);
	for (const button of $('metricButtons').querySelectorAll('button')) button.classList.toggle('on', button.dataset.metric === metric);
});
$('backToLive').addEventListener('click', backToLive);
$('alignWaves').addEventListener('change', (e) => inspector.setAlign(e.target.checked));
$('corrReset').addEventListener('click', () => inspector.resetCorrelation());
$('waveReset').addEventListener('click', () => inspector.resetWaveform());
$('playButton').addEventListener('click', () => (app.audio ? stopPlayback() : startPlayback()));

initTheme($('themeToggle'));
onThemeChange(() => {
	timeline.build();
	timeline.setSelection(app.selection);
	inspector.rebuild();
	updateCards();
});

// While measuring, keep the running session's duration and size in the list current.
setInterval(() => { if (app.state && app.state.running) sessions.refresh(); }, 5000);

// Start
updateRangeButtons();
$('metricButtons').querySelector('[data-metric="confidence"]').classList.add('on');
api.state().then((state) => {
	onState(state);
	subscribe({
		onState,
		onMeasurements,
		onDisk,
		onConnection: (connected) => {
			if (!connected) showError('Connection to the analyzer lost, reconnecting…');
			else if (app.state) renderHeader();
		},
	});
}).catch((e) => showError(`Cannot reach the analyzer: ${e.message}`));
