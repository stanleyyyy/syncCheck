// Sync Analyzer web UI.
import { api, subscribe } from './api.js';
import { formatMs, formatTime, pairLabel, quality, slope, el } from './format.js';
import { initTheme, onThemeChange, seriesColor } from './theme.js';
import { Timeline } from './timeline.js';
import { Inspector } from './inspector.js';
import { SettingsDrawer } from './settings.js';
import { SignalDialog } from './signal.js';

const $ = (id) => document.getElementById(id);

const app = {
	state: null,
	sessionId: 0,
	store: { t: [], pairs: [] },
	labels: [],
	selection: null,	// selected session time, null = live
	inspectTimer: null,
	inspectSeq: 0,
};

const timeline = new Timeline({
	delayEl: $('delayChart'), metricEl: $('metricChart'), overviewEl: $('overviewChart'),
	onSelect: (time) => select(time),
	onFollowChange: (follow) => { $('follow').checked = follow; updateRangeButtons(); updateCards(); },
});
const inspector = new Inspector({ corrEl: $('corrChart'), waveEl: $('waveChart'), tableEl: $('inspectTable'), noAudioEl: $('noAudio') });
const settingsDrawer = new SettingsDrawer({ drawer: $('settingsDrawer'), scrim: $('scrim'), body: $('settingsBody'),
	onSaved: (settings) => { app.state.settings = settings; renderHeader(); } });
const signalDialog = new SignalDialog({ dialog: $('signalDialog'), body: $('signalBody'), download: $('downloadSignal') });

function showError(message) {
	$('errorBanner').textContent = message || '';
	$('errorBanner').classList.toggle('hidden', !message);
}

function sessionMode() {
	return app.state && app.state.session ? app.state.session.mode : 'channels';
}

// Header: input summary, status, start/stop.
function renderHeader() {
	const state = app.state;
	if (!state) return;
	const s = state.settings;
	const session = state.session;
	let source;
	if (session && (state.running || session.duration)) {
		source = `${session.input} · ${session.channels} ch · ${session.sampleRate / 1000} kHz`;
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
	$('exportAudio').classList.toggle('disabled', !(session && session.audio));
	$('exportCsv').classList.toggle('disabled', !app.store.t.length);
}

// Pair cards: latest values (or values at the selected moment), quality and drift over the visible range.
function updateCards() {
	const container = $('pairs');
	const store = app.store;
	if (!app.labels.length) {
		container.replaceChildren();
		return;
	}
	let index = store.t.length - 1;
	if (app.selection !== null) index = nearestIndex(app.selection);
	const [from, to] = timeline.visibleIndexRange();

	container.replaceChildren(...app.labels.map((label, i) => {
		const pair = store.pairs[i];
		const has = pair && index >= 0;
		const delay = has ? pair.delay[index] : NaN;
		const confidence = has ? pair.confidence[index] : 0;
		const pslr = has ? pair.pslr[index] : 0;
		const q = quality(pslr, confidence);
		const drift = pair ? slope(store.t, pair.delay, from, to) * 60 : NaN;
		const card = el('button', { class: `card${timeline.visible[i] === false ? ' off' : ''}`, type: 'button',
			title: 'Show or hide this pair in the charts',
			onclick: () => { timeline.setVisible(i, timeline.visible[i] === false); updateCards(); } },
			el('div', { class: 'label' }, el('span', { class: 'key', style: `background:${seriesColor(i)}` }), label),
			el('div', { class: 'value' }, has ? formatMs(delay) : '–', el('small', { text: 'ms' })),
			el('div', { class: 'meta', text: has ? `${(confidence * 100).toFixed(0)} % confidence · PSLR ${pslr.toFixed(1)} dB` : 'waiting for data' }),
			el('div', { class: 'meta', text: Number.isFinite(drift) ? `Drift ${formatMs(drift, 3)} ms/min in view` : '' }),
			has ? el('span', { class: 'quality', 'data-q': q.key }, el('span', { class: 'icon' }), q.label) : null);
		return card;
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

function updateElapsed() {
	const t = app.store.t;
	$('elapsed').textContent = t.length ? formatTime(t[t.length - 1]) : '';
}

function updateRangeButtons() {
	for (const button of $('rangeButtons').querySelectorAll('button')) {
		button.classList.toggle('on', timeline.follow && Number(button.dataset.range) === timeline.rangeSeconds);
	}
}

function updateVisibility() {
	const hasData = app.labels.length > 0;
	$('empty').classList.toggle('hidden', hasData);
	$('pairs').classList.toggle('hidden', !hasData);
	$('timelinePanel').classList.toggle('hidden', !hasData);
	$('inspectPanel').classList.toggle('hidden', !hasData);
}

// A new session: reset the data and rebuild the charts.
function startSession(session) {
	app.sessionId = session ? session.id : 0;
	app.store = { t: [], pairs: [] };
	app.labels = session ? session.pairs.map((p) => pairLabel(p, session.mode)) : [];
	app.selection = null;
	app.store.pairs = app.labels.map(() => ({ delay: [], confidence: [], pslr: [], refRms: [], sigRms: [] }));
	updateVisibility();
	timeline.setPairs(app.labels);
	timeline.setData(app.store);
	inspector.setPairs(app.labels);
	updateCards();
	updateInspectorHeader();
}

function onState(state) {
	const previous = app.state;
	app.state = state;
	const session = state.session;
	if (!session) {
		if (app.sessionId) startSession(null);
	} else if (session.id !== app.sessionId || (session.pairs.length && session.pairs.length !== app.labels.length)) {
		startSession(session);
	}
	renderHeader();
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
	if (app.selection === null) scheduleInspect(1000);
}

// Inspector: the selected moment or, without a selection, the latest measurement.
function updateInspectorHeader() {
	const live = app.selection === null;
	const running = app.state && app.state.running;
	$('inspectTime').textContent = live ? (running ? '' : 'last measurement') : `at ${formatTime(app.selection, true)}`;
	$('liveBadge').classList.toggle('hidden', !(live && running));
	$('backToLive').classList.toggle('hidden', live);
	$('backToLive').textContent = running ? 'Back to live' : 'Show latest';
}

function scheduleInspect(delay) {
	if (app.inspectTimer) return;
	app.inspectTimer = setTimeout(() => { app.inspectTimer = null; inspect(); }, delay);
}

async function inspect() {
	if (!app.sessionId || !app.store.t.length) return;
	const seq = ++app.inspectSeq;
	const live = app.selection === null;
	const params = { range: $('corrRange').value, window: $('waveWindow').value };
	if (live) params.live = 1; else params.t = app.selection;
	try {
		const result = await api.inspect(params);
		if (seq !== app.inspectSeq) return;
		$('inspectError').classList.add('hidden');
		inspector.render(result, sessionMode());
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

// ← → step through measurements, Shift for 10 steps, Esc returns to live.
document.addEventListener('keydown', (e) => {
	if (e.target.closest('input, select, textarea, dialog') || !app.store.t.length) return;
	if (e.key === 'Escape') {
		if (!$('settingsDrawer').classList.contains('hidden')) settingsDrawer.close(); else backToLive();
		return;
	}
	if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
	const step = (e.key === 'ArrowLeft' ? -1 : 1) * (e.shiftKey ? 10 : 1);
	const current = app.selection === null ? app.store.t.length - 1 : nearestIndex(app.selection);
	const index = Math.max(0, Math.min(app.store.t.length - 1, current + step));
	const time = app.store.t[index];
	// Keep the selection in view.
	const range = timeline.currentRange();
	if (time < range.min || time > range.max) {
		const width = range.max - range.min;
		timeline.setXRange(time - width / 2, time + width / 2);
	}
	select(time);
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
$('scrim').addEventListener('click', () => settingsDrawer.close());
$('saveSettings').addEventListener('click', () => settingsDrawer.save());
$('openSignal').addEventListener('click', () => {
	const session = app.state.session;
	signalDialog.open({ ...app.state.settings, channels: session && session.channels ? session.channels : app.state.settings.channels });
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
$('corrRange').addEventListener('change', inspect);
$('waveWindow').addEventListener('change', inspect);
$('alignWaves').addEventListener('change', (e) => inspector.setAlign(e.target.checked));

initTheme($('themeToggle'));
onThemeChange(() => {
	timeline.build();
	timeline.setSelection(app.selection);
	inspector.rebuild();
	updateCards();
});

// Start
updateRangeButtons();
$('metricButtons').querySelector('[data-metric="confidence"]').classList.add('on');
api.state().then((state) => {
	onState(state);
	subscribe({
		onState,
		onMeasurements,
		onConnection: (connected) => {
			if (!connected) showError('Connection to the analyzer lost, reconnecting…');
			else if (app.state) renderHeader();
		},
	});
}).catch((e) => showError(`Cannot reach the analyzer: ${e.message}`));
