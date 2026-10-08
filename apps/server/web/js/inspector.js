// Inspector: correlation and waveforms at one moment of the session.
// Charts zoom with the mouse wheel and move by dragging; data for the new view is fetched when the gesture ends.
import { chartColors, seriesColor } from './theme.js';
import { formatMs, el } from './format.js';

const FONT = '12px system-ui, -apple-system, "Segoe UI", sans-serif';
const HEIGHT = { value: 190 };
const OVERVIEW_HEIGHT = 60;
const DEFAULT_CORR = { min: -10, max: 10 };
// The moment is the end of the analyzed block, so the waveform view ends there.
const DEFAULT_WAVE = { min: -5, max: 0 };

// Both charts share the height the window has left.
function fitHeight() {
	HEIGHT.value = Math.max(150, Math.min(340, (window.innerHeight - 460) / 2));
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

// Run move(dx) while the mouse is dragged, end() when it is released.
function onDrag(element, start, move, end) {
	element.addEventListener('mousedown', (e) => {
		if (e.button !== 0) return;
		const x0 = e.clientX;
		start(e);
		element.classList.add('dragging');
		const onMove = (m) => move(m.clientX - x0, m);
		const onUp = () => {
			window.removeEventListener('mousemove', onMove);
			window.removeEventListener('mouseup', onUp);
			element.classList.remove('dragging');
			end();
		};
		window.addEventListener('mousemove', onMove);
		window.addEventListener('mouseup', onUp);
		e.preventDefault();
	});
}

export class Inspector {
	constructor({ corrEl, waveEl, overviewEl, keysEl, tableEl, noAudioEl, corrReadout, waveReadout, onViewChange }) {
		this.corrEl = corrEl;
		this.waveEl = waveEl;
		this.overviewEl = overviewEl;
		this.keysEl = keysEl;
		this.tableEl = tableEl;
		this.noAudioEl = noAudioEl;
		this.corrReadout = corrReadout;
		this.waveReadout = waveReadout;
		this.onViewChange = onViewChange;
		this.result = null;
		this.labels = [];
		this.align = false;
		this.maxLag = 340;
		this.corrView = { ...DEFAULT_CORR };
		this.waveView = { ...DEFAULT_WAVE };
		this.frozenWaveY = null;
		this.wheelTimer = null;
		this.corr = this.wave = this.overview = null;
		this.dragTimer = null;
		this.reserveHeights();
		new ResizeObserver(() => this.resize()).observe(corrEl);
		window.addEventListener('resize', () => this.resize());
		this.updateReadouts();
	}

	setPairs(labels, maxLagMs) {
		this.labels = labels;
		this.maxLag = maxLagMs || this.maxLag;
		this.clear();
	}

	// Keep the chart areas their size before data arrives, so the page does not jump.
	reserveHeights() {
		fitHeight();
		this.corrEl.style.minHeight = `${HEIGHT.value}px`;
		this.waveEl.style.minHeight = `${HEIGHT.value}px`;
		this.overviewEl.style.minHeight = `${OVERVIEW_HEIGHT}px`;
	}

	// Parameters for /api/inspect: the visible ranges plus a margin on both sides, so dragging shows data right away.
	params() {
		const c = this.corrView;
		const range = Math.max(Math.abs(c.min), Math.abs(c.max)) + (c.max - c.min);
		const w = this.waveView;
		return { range: Math.min(range, this.maxLag).toFixed(3), window: (3 * (w.max - w.min)).toFixed(3), offset: ((w.min + w.max) / 2).toFixed(3),
			align: this.align ? 1 : 0 };
	}

	// While dragging, fetch new data every ~80 ms so the charts follow the mouse.
	fetchWhileDragging() {
		if (this.dragTimer) return;
		this.dragTimer = setTimeout(() => { this.dragTimer = null; this.onViewChange(true); }, 80);
	}

	// Aligned waveforms are read shifted by the server.
	setAlign(align) {
		this.align = align;
		this.onViewChange();
	}

	clear() {
		this.corr?.destroy();
		this.wave?.destroy();
		this.overview?.destroy();
		this.corr = this.wave = this.overview = null;
		this.waveKey = this.corrKey = null;
		this.result = null;
		this.tableEl.replaceChildren();
		this.keysEl.replaceChildren();
	}

	rebuild() {
		const result = this.result;
		this.clear();
		if (result) this.render(result);
	}

	render(result) {
		fitHeight();
		this.result = result;
		this.renderCorrelation(result);
		this.renderWaveform(result);
		this.renderTable(result);
	}

	updateReadouts() {
		const c = this.corrView;
		this.corrReadout.textContent = `${formatMs(c.min, 2)} … ${formatMs(c.max, 2)} ms`;
		const span = this.waveView.max - this.waveView.min;
		this.waveReadout.textContent = `${span < 10 ? span.toFixed(2) : span.toFixed(0)} ms window`;
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

	setWaveView(view, fetch = true) {
		const block = this.result && this.result.blockMs ? this.result.blockMs : 1000;
		const width = Math.min(Math.max(view.max - view.min, 0.2), block);
		// Keep the view inside the analyzed block (it ends at the moment, 0 ms).
		const max = Math.min(0, Math.max(-block + width, view.max));
		this.waveView = { min: max - width, max };
		this.wave?.setScale('x', this.waveView);
		this.overview?.redraw(false, false);
		this.updateReadouts();
		if (fetch) this.onViewChange();
	}

	resetCorrelation() {
		this.setCorrView(DEFAULT_CORR);
	}

	resetWaveform() {
		this.setWaveView(DEFAULT_WAVE);
	}

	// Fit the correlation view to the measured delays.
	fitCorrelation() {
		const delays = this.result ? this.result.pairs.map((p) => p.delay) : [0];
		const min = Math.min(0, ...delays), max = Math.max(0, ...delays);
		const margin = Math.max(1, (max - min) * 0.3);
		this.setCorrView({ min: min - margin, max: max + margin });
	}

	// Ctrl+wheel (or pinch) zooms around the mouse, a horizontal swipe moves; the plain wheel scrolls the page.
	wheel(u, e, apply, freeze, unfreeze) {
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
		this.fetchWhileDragging();
		clearTimeout(this.wheelTimer);
		this.wheelTimer = setTimeout(() => { unfreeze?.(); this.onViewChange(); }, 250);
	}

	// Dragging moves the view; data is fetched while dragging and once more when the mouse is released.
	bindDrag(u, apply, freeze, unfreeze) {
		let view;
		onDrag(u.over,
			() => { view = { min: u.scales.x.min, max: u.scales.x.max }; freeze?.(); },
			(dx) => {
				const shift = -dx * (view.max - view.min) / Math.max(u.over.clientWidth, 1);
				apply({ min: view.min + shift, max: view.max + shift }, false);
				this.fetchWhileDragging();
			},
			() => { unfreeze?.(); this.onViewChange(); });
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
		this.corr = new uPlot({
			width: this.corrEl.clientWidth || 500, height: HEIGHT.value, legend: { show: false },
			series: [{}, ...result.pairs.map((p, i) => ({ label: this.labels[i], stroke: seriesColor(i), width: 2, points: { show: false } }))],
			scales: { x: { time: false, range: () => [this.corrView.min, this.corrView.max] }, y: { range: [0, 1.05] } },
			axes: axes(colors, 'Lag (ms)', 'Correlation'),
			cursor: { points: { show: false }, drag: { x: false, y: false }, bind: { dblclick: () => null } },
			hooks: {
				draw: [(u) => this.drawPeaks(u)],
				ready: [(u) => {
					u.over.addEventListener('wheel', (e) => this.wheel(u, e, (v, f) => this.setCorrView(v, f)), { passive: false });
					this.bindDrag(u, (v, f) => this.setCorrView(v, f));
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
			ctx.strokeStyle = seriesColor(i);
			ctx.lineWidth = devicePixelRatio;
			ctx.setLineDash([3 * devicePixelRatio, 3 * devicePixelRatio]);
			ctx.beginPath();
			ctx.moveTo(x, u.bbox.top);
			ctx.lineTo(x, u.bbox.top + u.bbox.height);
			ctx.stroke();
		});
		ctx.restore();
	}

	// --- waveforms

	// Channels shown: the reference in ink, the compared ones colored like their pair.
	shownChannels(result) {
		const colors = chartColors();
		const ref = result.pairs.length ? result.pairs[0].ref : 0;
		if (result.pairs.every((p) => p.ref === p.sig)) {
			return [{ channel: ref, label: `Ch ${ref + 1}`, color: colors.ink2, delay: 0 }];
		}
		return [{ channel: ref, label: `Ch ${ref + 1} (reference)`, color: colors.ink2, delay: 0 },
			...result.pairs.map((p, i) => ({ channel: p.sig, label: `Ch ${p.sig + 1}`, color: seriesColor(i), delay: p.delay }))];
	}

	// uPlot data/series/bands for an envelope: a line per channel, or min/max bands when decimated.
	traces(wave, shown, sampleRate) {
		const n = wave.channels[0] ? wave.channels[0].min.length : 0;
		const xs = Array.from({ length: n }, (_, i) => wave.startMs + i * wave.stepMs);
		const data = [xs];
		const series = [{}];
		const bands = [];
		const decimated = wave.stepMs * sampleRate / 1000 > 1.5;
		// The reference is drawn wide and light underneath, so channels that line up stay visible.
		for (const [index, s] of shown.entries()) {
			const trace = wave.channels.find((c) => c.channel === s.channel);
			if (!trace) continue;
			const reference = index === 0 && shown.length > 1;
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

	renderWaveform(result, force = false) {
		const wave = result.waveform;
		this.noAudioEl.classList.toggle('hidden', !!wave);
		if (!wave) {
			this.wave?.destroy();
			this.overview?.destroy();
			this.wave = this.overview = null;
			this.waveKey = null;
			this.keysEl.replaceChildren();
			return;
		}

		const colors = chartColors();
		const shown = this.shownChannels(result);
		const { data, series, bands, key } = this.traces(wave, shown, result.sampleRate);

		if (this.wave && this.waveKey === key && !force) {
			this.wave.setData(data, true);
		} else {
			this.wave?.destroy();
			this.waveKey = key;
			const freeze = () => { this.frozenWaveY = [this.wave.scales.y.min, this.wave.scales.y.max]; };
			const unfreeze = () => { this.frozenWaveY = null; };
			this.wave = new uPlot({
				width: this.waveEl.clientWidth || 500, height: HEIGHT.value, legend: { show: false }, series, bands,
				scales: { x: { time: false, range: () => [this.waveView.min, this.waveView.max] }, y: { range: (u, min, max) => this.waveY(u, min, max) } },
				axes: axes(colors, 'Time relative to the moment (ms)', 'Amplitude'),
				cursor: { points: { show: false }, drag: { x: false, y: false }, bind: { dblclick: () => null } },
				hooks: {
					ready: [(u) => {
						u.over.addEventListener('wheel', (e) => this.wheel(u, e, (v, f) => this.setWaveView(v, f), freeze, unfreeze), { passive: false });
						this.bindDrag(u, (v, f) => this.setWaveView(v, f), freeze, unfreeze);
						u.over.addEventListener('dblclick', () => this.resetWaveform());
					}],
				},
			}, data, this.waveEl);
		}

		this.renderOverview(result, shown, colors);
		this.keysEl.replaceChildren(...shown.map((s) => el('span', {}, el('span', { class: 'key', style: `background:${s.color}` }), s.label)));
	}

	// Overview of the analyzed block with the visible waveform window; click or drag to move the window.
	renderOverview(result, shown, colors) {
		if (!result.overview) {
			this.overview?.destroy();
			this.overview = null;
			return;
		}
		const { data, series, bands, key } = this.traces(result.overview, shown, result.sampleRate);
		if (this.overview && this.overviewKey === key) {
			this.overview.setData(data, true);
			return;
		}
		this.overview?.destroy();
		this.overviewKey = key;
		const block = () => (this.result && this.result.blockMs) || 1000;
		const moveTo = (clientX) => {
			const u = this.overview;
			const at = u.posToVal(clientX - u.over.getBoundingClientRect().left, 'x');
			const width = this.waveView.max - this.waveView.min;
			this.setWaveView({ min: at - width / 2, max: at + width / 2 }, false);
		};
		this.overview = new uPlot({
			width: this.overviewEl.clientWidth || 500, height: OVERVIEW_HEIGHT, legend: { show: false }, series, bands,
			scales: { x: { time: false, range: () => [-block(), 0] }, y: { range: (u, min, max) => this.waveY(u, min, max) } },
			axes: [{ stroke: colors.muted, font: FONT, size: 22, grid: { show: false }, ticks: { stroke: colors.axis, size: 3 }, values: tickValues },
				{ size: 58, values: (u, vals) => vals.map(() => ''), grid: { show: false }, ticks: { show: false } }],
			cursor: { show: false, drag: { x: false, y: false } },
			hooks: {
				draw: [(u) => this.drawWindow(u, colors)],
				ready: [(u) => {
					onDrag(u.over, (e) => { moveTo(e.clientX); this.fetchWhileDragging(); },
						(dx, m) => { moveTo(m.clientX); this.fetchWhileDragging(); }, () => this.onViewChange());
					// Ctrl+wheel or a horizontal swipe moves the window.
					u.over.addEventListener('wheel', (e) => {
						const horizontal = Math.abs(e.deltaX) > Math.abs(e.deltaY);
						if (!(e.ctrlKey || e.metaKey) && !horizontal) return;
						e.preventDefault();
						const width = this.waveView.max - this.waveView.min;
						const shift = width * (horizontal ? e.deltaX : e.deltaY) / 200;
						this.setWaveView({ min: this.waveView.min + shift, max: this.waveView.max + shift }, false);
						this.fetchWhileDragging();
						clearTimeout(this.wheelTimer);
						this.wheelTimer = setTimeout(() => this.onViewChange(), 250);
					}, { passive: false });
				}],
			},
		}, data, this.overviewEl);
	}

	drawWindow(u, colors) {
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
		ctx.restore();
	}

	renderTable(result) {
		const head = el('tr', {}, el('th', { text: 'Pair' }), el('th', { class: 'right', text: 'Delay at this moment' }),
			el('th', { class: 'right', text: 'PSLR' }));
		const rows = result.pairs.map((p, i) => el('tr', {},
			el('td', {}, el('span', { class: 'row-inline' }, el('span', { class: 'key', style: `background:${seriesColor(i)}` }), this.labels[i] || '')),
			el('td', { class: 'right', text: `${formatMs(p.delay)} ms` }),
			el('td', { class: 'right', text: `${p.pslr.toFixed(1)} dB` })));
		this.tableEl.replaceChildren(el('thead', {}, head), el('tbody', {}, rows));
	}

	resize() {
		this.reserveHeights();
		this.corr?.setSize({ width: this.corrEl.clientWidth, height: HEIGHT.value });
		this.wave?.setSize({ width: this.waveEl.clientWidth, height: HEIGHT.value });
		this.overview?.setSize({ width: this.overviewEl.clientWidth, height: OVERVIEW_HEIGHT });
	}
}
