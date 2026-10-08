// Inspector: correlation at one moment of the session (the cursor) and the waveforms of the whole recording.
// Charts zoom with Ctrl+wheel and move by dragging; data for the new view is fetched while doing so.
// The waveform view moves freely through the recording; clicking it puts the cursor (the inspected moment) there.
import { api } from './api.js';
import { chartColors, seriesColor } from './theme.js';
import { formatMs, el } from './format.js';
import { bindGestures } from './gestures.js';
import { setChartSize, viewportHeight } from './layout.js';

const FONT = '12px system-ui, -apple-system, "Segoe UI", sans-serif';
const HEIGHT = { value: 190 };
const OVERVIEW_HEIGHT = 60;
const DEFAULT_CORR = { min: -10, max: 10 };
// Waveform view widths in s; the default shows the middle of the analyzed block.
const DEFAULT_WAVE_WIDTH = 0.005;
const MIN_WAVE_WIDTH = 0.0002;
const MAX_WAVE_WIDTH = 30;

// Both charts share the height the window has left.
function fitHeight() {
	HEIGHT.value = Math.max(150, Math.min(340, (viewportHeight() - 460) / 2));
}

// Tick labels with as many decimals as the tick spacing needs.
function tickValues(u, vals, axis, space, incr) {
	const decimals = Math.max(0, Math.min(7, -Math.floor(Math.log10(incr || 1))));
	return vals.map((v) => v.toFixed(decimals));
}

function axes(colors, xLabel, yLabel) {
	const common = { stroke: colors.muted, font: FONT, labelFont: FONT, grid: { stroke: colors.grid, width: 1 },
		ticks: { stroke: colors.axis, width: 1, size: 4 }, values: tickValues };
	return [
		{ ...common, label: xLabel, labelSize: 16, size: 36 },
		{ ...common, label: yLabel, labelSize: 16, size: 58 },
	];
}

function verticalLine(u, x, color, dash) {
	const ctx = u.ctx;
	ctx.strokeStyle = color;
	ctx.lineWidth = devicePixelRatio;
	ctx.setLineDash(dash ? [3 * devicePixelRatio, 3 * devicePixelRatio] : []);
	ctx.beginPath();
	ctx.moveTo(x, u.bbox.top);
	ctx.lineTo(x, u.bbox.top + u.bbox.height);
	ctx.stroke();
}

export class Inspector {
	constructor({ corrEl, waveEl, overviewEl, keysEl, tableEl, noAudioEl, corrReadout, waveReadout, onViewChange, onSelect, levels }) {
		this.corrEl = corrEl;
		this.waveEl = waveEl;
		this.overviewEl = overviewEl;
		this.keysEl = keysEl;
		this.tableEl = tableEl;
		this.noAudioEl = noAudioEl;
		this.corrReadout = corrReadout;
		this.waveReadout = waveReadout;
		this.onViewChange = onViewChange;
		this.onSelect = onSelect;
		this.levels = levels;
		this.result = null;
		this.labels = [];
		this.align = false;
		this.maxLag = 340;
		this.corrView = { ...DEFAULT_CORR };
		this.resetWaveState();
		this.frozenWaveY = null;
		this.wheelTimer = null;
		this.corr = this.wave = this.overview = null;
		this.dragTimer = null;
		this.reserveHeights();
		new ResizeObserver(() => this.resize()).observe(corrEl);
		window.addEventListener('resize', () => this.resize());
		this.updateReadouts();
	}

	resetWaveState() {
		this.cursor = null;		// inspected moment (s), the middle of the analyzed block
		this.waveView = null;	// visible session time range (s)
		this.followLive = true;	// the view stays at the latest moment while live
		this.waveData = null;
		this.duration = 0;
		this.waveSeq = this.waveRendered = 0;
		this.waveDelays = '';
		this.overviewData = null;
	}

	setPairs(labels, maxLagMs) {
		this.labels = labels;
		this.maxLag = maxLagMs || this.maxLag;
		this.resetWaveState();
		this.clear();
		// Rows in place before the first data, so the panel does not grow later.
		this.renderTable({ pairs: labels.map(() => ({ delay: NaN, pslr: NaN })) });
	}

	// Keep the chart areas their size before data arrives, so the page does not jump.
	reserveHeights() {
		fitHeight();
		this.corrEl.style.minHeight = `${HEIGHT.value}px`;
		this.waveEl.style.minHeight = `${HEIGHT.value}px`;
		this.overviewEl.style.minHeight = `${OVERVIEW_HEIGHT + 5}px`;
	}

	// Parameters for /api/inspect: the visible lag range plus a margin on both sides, so dragging shows data right away.
	params() {
		const c = this.corrView;
		const range = Math.max(Math.abs(c.min), Math.abs(c.max)) + (c.max - c.min);
		return { range: Math.min(range, this.maxLag).toFixed(3), window: 0.5 };
	}

	// The cursor was moved elsewhere (timeline, keys); null = back to the latest moment.
	setMoment(time) {
		if (time === null) {
			this.followLive = true;
			return;
		}
		this.cursor = time;
		this.revealCursor();
		this.redrawWave();
	}

	// Aligned waveforms are read shifted by the server.
	setAlign(align) {
		this.align = align;
		this.fetchWave();
	}

	clear() {
		this.corr?.destroy();
		this.wave?.destroy();
		this.overview?.destroy();
		this.corr = this.wave = this.overview = null;
		this.waveKey = this.corrKey = this.overviewKey = null;
		this.result = null;
		this.tableEl.replaceChildren();
		this.keysEl.replaceChildren();
	}

	rebuild() {
		const result = this.result;
		const wave = this.waveData;
		this.clear();
		if (result) this.render(result);
		if (wave) this.renderWaveform(wave);
	}

	render(result) {
		fitHeight();
		this.result = result;
		this.cursor = result.time;
		if (!this.waveView) {
			this.waveView = { min: this.cursor - DEFAULT_WAVE_WIDTH / 2, max: this.cursor + DEFAULT_WAVE_WIDTH / 2 };
		} else if (result.live && this.followLive) {
			const width = this.waveView.max - this.waveView.min;
			this.waveView = { min: this.cursor - width / 2, max: this.cursor + width / 2 };
		} else {
			this.revealCursor(false);
		}
		this.updateReadouts();
		this.renderCorrelation(result);
		this.renderTable(result);
		if (this.overviewData) this.updateOverview(); else this.fetchOverview();
		this.wave?.setScale('x', this.waveView);
		// New data when the view moved or the delays to align by changed.
		const view = this.waveData ? this.waveData.view : null;
		if (!view || view.min !== this.waveView.min || view.max !== this.waveView.max || this.alignDelays() !== this.waveDelays) {
			this.fetchWave();
		}
	}

	updateReadouts() {
		const c = this.corrView;
		this.corrReadout.textContent = `${formatMs(c.min, 2)} … ${formatMs(c.max, 2)} ms`;
		const span = this.waveView ? this.waveView.max - this.waveView.min : DEFAULT_WAVE_WIDTH;
		this.waveReadout.textContent = span < 1 ? `${(span * 1000).toFixed(span < 0.01 ? 2 : 0)} ms window` : `${span.toFixed(span < 10 ? 2 : 0)} s window`;
	}

	// --- view changes; fetch = false while a gesture is in progress

	setCorrView(view, fetch = true) {
		const width = Math.min(Math.max(view.max - view.min, 0.05), 2 * this.maxLag);
		const center = Math.max(-this.maxLag, Math.min(this.maxLag, (view.min + view.max) / 2));
		this.corrView = { min: center - width / 2, max: center + width / 2 };
		this.corr?.setScale('x', this.corrView);
		this.updateReadouts();
		if (fetch) this.onViewChange();
	}

	// End of the recording as far as known.
	sessionEnd() {
		const store = this.levels();
		const last = store.t.length ? store.t[store.t.length - 1] : 0;
		return Math.max(this.duration, last, this.cursor || 0);
	}

	setWaveView(view, fetch = true) {
		// The view always lies within the recording, so it can zoom out to the whole session at most.
		const end = this.sessionEnd();
		const width = Math.min(Math.max(view.max - view.min, MIN_WAVE_WIDTH), MAX_WAVE_WIDTH, Math.max(end, MIN_WAVE_WIDTH));
		const min = Math.max(0, Math.min(end - width, view.min));
		this.waveView = { min, max: min + width };
		this.wave?.setScale('x', this.waveView);
		this.overview?.redraw(false, false);
		this.updateReadouts();
		if (fetch) this.fetchWave();
	}

	// Moved by the user: stop following the latest moment.
	moveWaveView(view, fetch) {
		this.followLive = false;
		this.setWaveView(view, fetch);
	}

	// Bring the cursor into view, keeping the zoom.
	revealCursor(fetch = true) {
		if (this.cursor === null || !this.waveView) return;
		const { min, max } = this.waveView;
		if (this.cursor >= min && this.cursor <= max) return;
		const width = max - min;
		this.setWaveView({ min: this.cursor - width / 2, max: this.cursor + width / 2 }, fetch);
	}

	resetCorrelation() {
		this.setCorrView(DEFAULT_CORR);
	}

	// The middle of the analyzed block, at the cursor.
	resetWaveform() {
		if (this.cursor === null) return;
		this.followLive = true;
		this.setWaveView({ min: this.cursor - DEFAULT_WAVE_WIDTH / 2, max: this.cursor + DEFAULT_WAVE_WIDTH / 2 });
	}

	// Fit the correlation view to the measured delays.
	fitCorrelation() {
		const delays = this.result ? this.result.pairs.map((p) => p.delay) : [0];
		const min = Math.min(0, ...delays), max = Math.max(0, ...delays);
		const margin = Math.max(1, (max - min) * 0.3);
		this.setCorrView({ min: min - margin, max: max + margin });
	}

	// While a gesture is in progress, refresh about every 80 ms so the charts follow the mouse.
	refreshWhileDragging(refresh) {
		if (this.dragTimer) return;
		this.dragTimer = setTimeout(() => { this.dragTimer = null; refresh(true); }, 80);
	}

	// Ctrl+wheel (or pinch) zooms around the mouse, a horizontal swipe moves; the plain wheel scrolls the page.
	wheel(u, e, apply, refresh, freeze, unfreeze) {
		const horizontal = Math.abs(e.deltaX) > Math.abs(e.deltaY);
		if (!(e.ctrlKey || e.metaKey) && !horizontal) return;
		e.preventDefault();
		freeze?.();
		const { min, max } = u.scales.x;
		if (horizontal) {
			const shift = (max - min) * e.deltaX / Math.max(u.over.clientWidth, 1);
			apply({ min: min + shift, max: max + shift }, false);
		} else {
			const at = u.posToVal(e.offsetX, 'x');
			const factor = Math.exp(e.deltaY * 0.002);
			apply({ min: at - (at - min) * factor, max: at + (max - at) * factor }, false);
		}
		this.refreshWhileDragging(refresh);
		clearTimeout(this.wheelTimer);
		this.wheelTimer = setTimeout(() => { unfreeze?.(); refresh(false); }, 250);
	}

	// Dragging moves the view and pinching zooms; data is fetched while doing so and once more at the end.
	bindDrag(u, apply, refresh, freeze, unfreeze, onTap) {
		let view;
		const capture = () => { view = { min: u.scales.x.min, max: u.scales.x.max }; freeze?.(); };
		bindGestures(u.over, {
			onStart: capture,
			onDrag: (dx) => {
				const shift = -dx * (view.max - view.min) / Math.max(u.over.clientWidth, 1);
				apply({ min: view.min + shift, max: view.max + shift }, false);
				this.refreshWhileDragging(refresh);
			},
			onEnd: (moved) => { unfreeze?.(); if (moved) refresh(false); },
			onTap,
			onPinchStart: capture,
			onPinch: (scale, clientX) => {
				const at = view.min + (view.max - view.min) * (clientX - u.over.getBoundingClientRect().left) / Math.max(u.over.clientWidth, 1);
				const width = (view.max - view.min) / scale;
				const ratio = (at - view.min) / (view.max - view.min);
				apply({ min: at - width * ratio, max: at + width * (1 - ratio) }, false);
				this.refreshWhileDragging(refresh);
			},
		});
	}

	// --- correlation

	renderCorrelation(result) {
		const colors = chartColors();
		const data = [result.lag, ...result.pairs.map((p) => p.corr)];
		const key = result.pairs.length + ':' + this.labels.join();
		if (this.corr && this.corrKey === key) {
			this.corr.setData(data, true);
			return;
		}
		this.corr?.destroy();
		this.corrKey = key;
		const refresh = (dragging) => this.onViewChange(dragging);
		this.corr = new uPlot({
			width: this.corrEl.clientWidth || 500, height: HEIGHT.value, legend: { show: false },
			series: [{}, ...result.pairs.map((p, i) => ({ label: this.labels[i], stroke: seriesColor(i), width: 2, points: { show: false } }))],
			scales: { x: { time: false, range: () => [this.corrView.min, this.corrView.max] }, y: { range: [0, 1.05] } },
			axes: axes(colors, 'Lag (ms)', 'Correlation'),
			cursor: { points: { show: false }, drag: { x: false, y: false }, bind: { dblclick: () => null } },
			hooks: {
				draw: [(u) => this.drawPeaks(u)],
				ready: [(u) => {
					u.over.addEventListener('wheel', (e) => this.wheel(u, e, (v, f) => this.setCorrView(v, f), refresh), { passive: false });
					this.bindDrag(u, (v, f) => this.setCorrView(v, f), refresh);
					u.over.addEventListener('dblclick', () => this.fitCorrelation());
				}],
			},
		}, data, this.corrEl);
	}

	// Mark each pair's measured delay on the correlation chart.
	drawPeaks(u) {
		if (!this.result) return;
		const ctx = u.ctx;
		ctx.save();
		this.result.pairs.forEach((p, i) => {
			const x = u.valToPos(p.delay, 'x', true);
			if (x < u.bbox.left || x > u.bbox.left + u.bbox.width) return;
			verticalLine(u, x, seriesColor(i), true);
		});
		ctx.restore();
	}

	// --- waveforms

	// Channels shown: the reference in ink, the compared ones colored like their pair.
	shownChannels(result) {
		const colors = chartColors();
		const ref = result.pairs.length ? result.pairs[0].ref : 0;
		if (result.pairs.every((p) => p.ref === p.sig)) {
			return [{ channel: ref, label: `Ch ${ref + 1}`, color: colors.ink2, pair: 0, reference: true }];
		}
		return [{ channel: ref, label: `Ch ${ref + 1} (reference)`, color: colors.ink2, pair: 0, reference: true },
			...result.pairs.map((p, i) => ({ channel: p.sig, label: `Ch ${p.sig + 1}`, color: seriesColor(i), pair: i, reference: false }))];
	}

	// Delays of the compared channels at the cursor, "channel:ms,...", when aligning.
	alignDelays() {
		if (!this.align || !this.result) return '';
		return this.result.pairs.filter((p) => p.sig !== p.ref).map((p) => `${p.sig}:${p.delay}`).join(',');
	}

	// Waveforms of the view plus a margin on both sides, so dragging shows data right away.
	async fetchWave() {
		if (!this.waveView || !this.result) return;
		const seq = ++this.waveSeq;
		const view = { ...this.waveView };
		const margin = Math.min(view.max - view.min, 2);
		const delays = this.alignDelays();
		try {
			const wave = await api.waveform({ from: (view.min - margin).toFixed(6), to: (view.max + margin).toFixed(6), delays });
			if (seq <= this.waveRendered) return;
			this.waveRendered = seq;
			this.duration = wave.duration;
			this.waveDelays = delays;
			wave.view = view;
			this.renderWaveform(wave);
			// The recording turned out shorter than the view.
			if (this.waveView.max > this.sessionEnd()) this.setWaveView(this.waveView);
		} catch (e) {
			if (seq !== this.waveSeq) return;
			this.waveData = null;
			this.noAudioEl.classList.remove('hidden');
			this.wave?.destroy();
			this.wave = null;
			this.waveKey = null;
		}
	}

	// uPlot data/series/bands for an envelope: a line per channel, or min/max bands when decimated.
	traces(wave, shown) {
		const n = wave.channels && wave.channels[0] ? wave.channels[0].min.length : 0;
		const xs = Array.from({ length: n }, (_, i) => wave.start + i * wave.step);
		const data = [xs];
		const series = [{}];
		const bands = [];
		const decimated = wave.step * wave.sampleRate > 1.5;
		// The reference is drawn wide and light underneath, so channels that line up stay visible.
		for (const s of shown) {
			const trace = wave.channels ? wave.channels.find((c) => c.channel === s.channel) : null;
			if (!trace) continue;
			const reference = s.reference && shown.length > 1;
			if (decimated) {
				data.push(trace.max, trace.min);
				series.push({ stroke: s.color, width: 1, points: { show: false } }, { stroke: s.color, width: 1, points: { show: false } });
				bands.push({ series: [series.length - 2, series.length - 1], fill: s.color + '40' });
			} else {
				data.push(trace.max);
				series.push({ stroke: reference ? s.color + '80' : s.color, width: reference ? 4 : 1.25, points: { show: false } });
			}
		}
		return { data, series, bands, key: `${shown.map((s) => s.channel + s.color).join()}:${decimated}` };
	}

	waveY(u, min, max) {
		if (this.frozenWaveY) return this.frozenWaveY;
		const m = Math.max(Math.abs(min || 0), Math.abs(max || 0), 1e-6) * 1.1;
		return [-m, m];
	}

	redrawWave() {
		this.wave?.redraw(false, false);
		this.overview?.redraw(false, false);
	}

	renderWaveform(wave, force = false) {
		this.waveData = wave;
		this.noAudioEl.classList.add('hidden');
		if (!this.result) return;
		const colors = chartColors();
		const shown = this.shownChannels(this.result);
		const { data, series, bands, key } = this.traces(wave, shown);

		if (this.wave && this.waveKey === key && !force) {
			this.wave.setData(data, true);
		} else {
			this.wave?.destroy();
			this.waveKey = key;
			const freeze = () => { this.frozenWaveY = [this.wave.scales.y.min, this.wave.scales.y.max]; };
			const unfreeze = () => { this.frozenWaveY = null; };
			const refresh = () => this.fetchWave();
			const select = (e) => {
				const u = this.wave;
				this.onSelect(u.posToVal(e.clientX - u.over.getBoundingClientRect().left, 'x'));
			};
			this.wave = new uPlot({
				width: this.waveEl.clientWidth || 500, height: HEIGHT.value, legend: { show: false }, series, bands,
				scales: { x: { time: false, range: () => [this.waveView.min, this.waveView.max] }, y: { range: (u, min, max) => this.waveY(u, min, max) } },
				axes: axes(colors, 'Session time (s)', 'Amplitude'),
				cursor: { points: { show: false }, drag: { x: false, y: false }, bind: { dblclick: () => null } },
				hooks: {
					drawClear: [(u) => this.drawBlock(u, colors)],
					draw: [(u) => this.drawCursor(u, colors)],
					ready: [(u) => {
						u.over.addEventListener('wheel', (e) => this.wheel(u, e, (v, f) => this.moveWaveView(v, f), refresh, freeze, unfreeze), { passive: false });
						this.bindDrag(u, (v, f) => this.moveWaveView(v, f), refresh, freeze, unfreeze, select);
						u.over.addEventListener('dblclick', () => this.resetWaveform());
					}],
				},
			}, data, this.waveEl);
		}
		this.keysEl.replaceChildren(...shown.map((s) => el('span', {}, el('span', { class: 'key', style: `background:${s.color}` }), s.label)));
		this.updateOverview();
	}

	// The analyzed block (centered on the cursor), shaded.
	drawBlock(u, colors) {
		if (this.cursor === null || !this.result || !this.result.blockMs) return;
		const half = this.result.blockMs / 2000;
		const left = Math.max(u.bbox.left, u.valToPos(this.cursor - half, 'x', true));
		const right = Math.min(u.bbox.left + u.bbox.width, u.valToPos(this.cursor + half, 'x', true));
		if (right <= left) return;
		const ctx = u.ctx;
		ctx.save();
		ctx.fillStyle = colors.accent;
		ctx.globalAlpha = 0.06;
		ctx.fillRect(left, u.bbox.top, right - left, u.bbox.height);
		ctx.restore();
	}

	drawCursor(u, colors) {
		if (this.cursor === null) return;
		const x = u.valToPos(this.cursor, 'x', true);
		if (x < u.bbox.left || x > u.bbox.left + u.bbox.width) return;
		u.ctx.save();
		verticalLine(u, x, colors.accent, true);
		u.ctx.restore();
	}

	// The recording grew: refresh the navigator, at most every 2 s.
	sessionGrew() {
		if (this.overviewTimer) return;
		this.overviewTimer = setTimeout(() => { this.overviewTimer = null; this.fetchOverview(); }, 2000);
	}

	async fetchOverview() {
		if (!this.result || this.overviewLoading) return;
		this.overviewLoading = true;
		try {
			const overview = await api.overview({ points: Math.round(Math.max(this.overviewEl.clientWidth, 300)) });
			this.overviewData = overview;
			this.duration = Math.max(this.duration, overview.duration);
			this.updateOverview();
		} catch (e) {
			// No audio; the waveform chart says so.
		} finally {
			this.overviewLoading = false;
		}
	}

	// Navigator: the waveforms of the whole recording, with the visible window and the cursor.
	updateOverview() {
		if (!this.result || !this.waveView || !this.overviewData) return;
		const { data, series, bands, key } = this.traces(this.overviewData, this.shownChannels(this.result));
		if (this.overview && this.overviewKey === key) {
			this.overview.setData(data, true);
			return;
		}
		this.overview?.destroy();
		this.overviewKey = key;
		const colors = chartColors();
		const moveTo = (clientX) => {
			const u = this.overview;
			const at = u.posToVal(clientX - u.over.getBoundingClientRect().left, 'x');
			const width = this.waveView.max - this.waveView.min;
			this.moveWaveView({ min: at - width / 2, max: at + width / 2 }, false);
		};
		const refresh = () => this.fetchWave();
		this.overview = new uPlot({
			width: this.overviewEl.clientWidth || 500, height: OVERVIEW_HEIGHT, legend: { show: false }, series, bands,
			scales: { x: { time: false, range: () => [0, Math.max(this.sessionEnd(), 1)] },
				y: { range: (u, min, max) => { const m = Math.max(Math.abs(min || 0), Math.abs(max || 0), 1e-6) * 1.1; return [-m, m]; } } },
			axes: [{ stroke: colors.muted, font: FONT, size: 22, grid: { show: false }, ticks: { stroke: colors.axis, size: 3 }, values: tickValues },
				{ size: 58, values: (u, vals) => vals.map(() => ''), grid: { show: false }, ticks: { show: false } }],
			cursor: { show: false, drag: { x: false, y: false } },
			hooks: {
				draw: [(u) => this.drawWindow(u, colors)],
				ready: [(u) => {
					bindGestures(u.over, {
						onStart: (e) => { moveTo(e.clientX); this.refreshWhileDragging(refresh); },
						onDrag: (dx, e) => { moveTo(e.clientX); this.refreshWhileDragging(refresh); },
						onEnd: refresh,
					});
					// Ctrl+wheel or a horizontal swipe moves the window.
					u.over.addEventListener('wheel', (e) => {
						const horizontal = Math.abs(e.deltaX) > Math.abs(e.deltaY);
						if (!(e.ctrlKey || e.metaKey) && !horizontal) return;
						e.preventDefault();
						const width = this.waveView.max - this.waveView.min;
						const shift = width * (horizontal ? e.deltaX : e.deltaY) / 200;
						this.moveWaveView({ min: this.waveView.min + shift, max: this.waveView.max + shift }, false);
						this.refreshWhileDragging(refresh);
						clearTimeout(this.wheelTimer);
						this.wheelTimer = setTimeout(refresh, 250);
					}, { passive: false });
				}],
			},
		}, data, this.overviewEl);
	}

	drawWindow(u, colors) {
		if (!this.waveView) return;
		const left = Math.max(u.bbox.left, u.valToPos(this.waveView.min, 'x', true));
		const right = Math.min(u.bbox.left + u.bbox.width, Math.max(left + 2, u.valToPos(this.waveView.max, 'x', true)));
		const ctx = u.ctx;
		ctx.save();
		ctx.fillStyle = colors.surface;
		ctx.globalAlpha = 0.6;
		ctx.fillRect(u.bbox.left, u.bbox.top, left - u.bbox.left, u.bbox.height);
		ctx.fillRect(right, u.bbox.top, u.bbox.left + u.bbox.width - right, u.bbox.height);
		ctx.globalAlpha = 1;
		ctx.strokeStyle = colors.accent;
		ctx.lineWidth = devicePixelRatio;
		ctx.strokeRect(left, u.bbox.top, right - left, u.bbox.height);
		if (this.cursor !== null) verticalLine(u, u.valToPos(this.cursor, 'x', true), colors.accent, true);
		ctx.restore();
	}

	renderTable(result) {
		const head = el('tr', {}, el('th', { text: 'Pair' }), el('th', { class: 'right', text: 'Delay at this moment' }),
			el('th', { class: 'right', text: 'PSLR' }));
		const rows = result.pairs.map((p, i) => el('tr', {},
			el('td', {}, el('span', { class: 'row-inline' }, el('span', { class: 'key', style: `background:${seriesColor(i)}` }), this.labels[i] || '')),
			el('td', { class: 'right', text: `${formatMs(p.delay)} ms` }),
			el('td', { class: 'right', text: Number.isFinite(p.pslr) ? `${p.pslr.toFixed(1)} dB` : '– dB' })));
		this.tableEl.replaceChildren(el('thead', {}, head), el('tbody', {}, rows));
	}

	resize() {
		this.reserveHeights();
		setChartSize(this.corr, this.corrEl.clientWidth, HEIGHT.value);
		setChartSize(this.wave, this.waveEl.clientWidth, HEIGHT.value);
		setChartSize(this.overview, this.overviewEl.clientWidth, OVERVIEW_HEIGHT);
	}
}
