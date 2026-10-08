// Inspector: correlation and waveforms at one moment of the session.
// Both charts zoom with the mouse wheel and move with Shift+wheel; the data is fetched for the visible range.
import { chartColors, seriesColor } from './theme.js';
import { formatMs, el } from './format.js';

const FONT = '12px system-ui, -apple-system, "Segoe UI", sans-serif';
const HEIGHT = { value: 190 };

// Both charts share the height the window has left.
function fitHeight() {
	HEIGHT.value = Math.max(160, Math.min(360, (window.innerHeight - 390) / 2));
}
const DEFAULT_LAG = 10;
const DEFAULT_WINDOW = 5;

function axes(colors, xLabel, yLabel, yValues) {
	const common = { stroke: colors.muted, font: FONT, labelFont: FONT, grid: { stroke: colors.grid, width: 1 },
		ticks: { stroke: colors.axis, width: 1, size: 4 } };
	return [
		{ ...common, label: xLabel, labelSize: 16, size: 36, values: (u, vals) => vals.map((v) => `${+v.toFixed(3)}`) },
		{ ...common, label: yLabel, labelSize: 16, size: 44, values: yValues },
	];
}

export class Inspector {
	constructor({ corrEl, waveEl, keysEl, tableEl, noAudioEl, corrReadout, waveReadout, onViewChange }) {
		this.corrEl = corrEl;
		this.waveEl = waveEl;
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
		this.corrView = { min: -DEFAULT_LAG, max: DEFAULT_LAG };
		this.waveView = { offset: 0, span: DEFAULT_WINDOW };
		this.corr = null;
		this.wave = null;
		new ResizeObserver(() => this.resize()).observe(corrEl);
		window.addEventListener('resize', () => this.resize());
		this.updateReadouts();
	}

	setPairs(labels, maxLagMs) {
		this.labels = labels;
		this.maxLag = maxLagMs || this.maxLag;
		this.clear();
	}

	// Parameters for /api/inspect matching the visible ranges.
	params() {
		const range = Math.max(Math.abs(this.corrView.min), Math.abs(this.corrView.max));
		return { range: Math.min(range, this.maxLag).toFixed(3), window: this.waveView.span.toFixed(3), offset: this.waveView.offset.toFixed(3) };
	}

	setAlign(align) {
		this.align = align;
		if (this.result) this.renderWaveform(this.result);
	}

	clear() {
		this.corr?.destroy();
		this.wave?.destroy();
		this.corr = this.wave = null;
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
		this.corrReadout.textContent = `${formatMs(c.min, 2)} … ${formatMs(c.max, 2)} ms · scroll to zoom, double-click to fit`;
		const w = this.waveView;
		this.waveReadout.textContent = `${w.span < 10 ? w.span.toFixed(2) : w.span.toFixed(0)} ms window · double-click to reset`;
	}

	// Zoom (factor > 1 zooms out) or pan a [min, max] view.
	static zoomView(min, max, factor, at) {
		const ratio = (at - min) / (max - min);
		const width = (max - min) * factor;
		return { min: at - width * ratio, max: at + width * (1 - ratio) };
	}

	wheel(u, e, apply) {
		e.preventDefault();
		const { min, max } = u.scales.x;
		const horizontal = Math.abs(e.deltaX) > Math.abs(e.deltaY);
		if (e.shiftKey || horizontal) {
			const shift = (max - min) * (horizontal ? e.deltaX : e.deltaY) / Math.max(u.over.clientWidth, 1);
			apply({ min: min + shift, max: max + shift });
		} else {
			apply(Inspector.zoomView(min, max, Math.exp(e.deltaY * 0.002), u.posToVal(e.offsetX, 'x')));
		}
	}

	setCorrView(view) {
		const width = Math.min(Math.max(view.max - view.min, 0.05), 2 * this.maxLag);
		const center = Math.max(-this.maxLag, Math.min(this.maxLag, (view.min + view.max) / 2));
		this.corrView = { min: center - width / 2, max: center + width / 2 };
		this.corr?.setScale('x', this.corrView);
		this.updateReadouts();
		this.onViewChange();
	}

	// Fit the view to the measured delays.
	fitCorrelation() {
		const delays = this.result ? this.result.pairs.map((p) => p.delay) : [0];
		const min = Math.min(0, ...delays), max = Math.max(0, ...delays);
		const margin = Math.max(1, (max - min) * 0.3);
		this.setCorrView({ min: min - margin, max: max + margin });
	}

	setWaveView(view) {
		const span = Math.min(Math.max(view.max - view.min, 0.2), 60000);
		this.waveView = { offset: (view.min + view.max) / 2, span };
		this.wave?.setScale('x', { min: view.min, max: view.max });
		this.updateReadouts();
		this.onViewChange();
	}

	renderCorrelation(result) {
		const colors = chartColors();
		const data = [result.lag, ...result.pairs.map((p) => p.corr)];
		const key = result.pairs.length + ':' + this.labels.join();
		if (!this.corr || this.corrKey !== key) {
			this.corr?.destroy();
			this.corrKey = key;
			this.corr = new uPlot({
				width: this.corrEl.clientWidth || 500, height: HEIGHT.value, legend: { show: false },
				series: [{}, ...result.pairs.map((p, i) => ({ label: this.labels[i], stroke: seriesColor(i), width: 2, points: { show: false } }))],
				scales: { x: { time: false, range: () => [this.corrView.min, this.corrView.max] }, y: { range: [0, 1.05] } },
				axes: axes(colors, 'Lag (ms)', 'Correlation', (u, vals) => vals.map((v) => v.toFixed(1))),
				cursor: { points: { show: false }, drag: { x: false, y: false }, bind: { dblclick: () => null } },
				hooks: {
					draw: [(u) => this.drawPeaks(u)],
					ready: [(u) => {
						u.over.addEventListener('wheel', (e) => this.wheel(u, e, (v) => this.setCorrView(v)), { passive: false });
						u.over.addEventListener('dblclick', () => this.fitCorrelation());
					}],
				},
			}, data, this.corrEl);
		} else {
			this.corr.setData(data, true);
		}
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

	renderWaveform(result) {
		const wave = result.waveform;
		this.noAudioEl.classList.toggle('hidden', !!wave);
		if (!wave) {
			this.wave?.destroy();
			this.wave = null;
			this.keysEl.replaceChildren();
			return;
		}

		const colors = chartColors();
		const n = wave.channels[0] ? wave.channels[0].min.length : 0;
		const xs = Array.from({ length: n }, (_, i) => wave.startMs + i * wave.stepMs);

		// Channels shown: the reference in ink, the compared ones colored like their pair.
		const shown = [];
		const ref = result.pairs.length ? result.pairs[0].ref : 0;
		if (result.pairs.every((p) => p.ref === p.sig)) {
			shown.push({ channel: ref, label: `Ch ${ref + 1}`, color: colors.ink2, delay: 0 });
		} else {
			shown.push({ channel: ref, label: `Ch ${ref + 1} (reference)`, color: colors.ink2, delay: 0 });
			result.pairs.forEach((p, i) => shown.push({ channel: p.sig, label: `Ch ${p.sig + 1}`, color: seriesColor(i), delay: p.delay }));
		}

		const data = [xs];
		const series = [{}];
		const bands = [];
		const decimated = wave.stepMs * result.sampleRate / 1000 > 1.5;
		for (const s of shown) {
			const trace = wave.channels.find((c) => c.channel === s.channel);
			if (!trace) continue;
			// Align: move a delayed channel back by its measured delay.
			const offset = this.align ? Math.round(s.delay / wave.stepMs) : 0;
			const shift = (values) => values.map((_, i) => {
				const k = i + offset;
				return k >= 0 && k < values.length ? values[k] : null;
			});
			if (decimated) {
				data.push(shift(trace.max), shift(trace.min));
				series.push({ label: s.label, stroke: s.color, width: 1, points: { show: false } },
					{ label: s.label, stroke: s.color, width: 1, points: { show: false } });
				bands.push({ series: [series.length - 2, series.length - 1], fill: s.color + '33' });
			} else {
				data.push(shift(trace.max));
				series.push({ label: s.label, stroke: s.color, width: 1.5, points: { show: false } });
			}
		}

		const view = { min: this.waveView.offset - this.waveView.span / 2, max: this.waveView.offset + this.waveView.span / 2 };
		this.wave?.destroy();
		this.wave = new uPlot({
			width: this.waveEl.clientWidth || 500, height: HEIGHT.value, legend: { show: false }, series, bands,
			scales: {
				x: { time: false, range: () => [view.min, view.max] },
				y: { range: (u, min, max) => { const m = Math.max(Math.abs(min), Math.abs(max), 1e-3) * 1.1; return [-m, m]; } },
			},
			axes: axes(colors, 'Time relative to the moment (ms)', 'Amplitude', (u, vals) => vals.map((v) => v.toFixed(2))),
			cursor: { points: { show: false }, drag: { x: false, y: false }, bind: { dblclick: () => null } },
			hooks: {
				ready: [(u) => {
					u.over.addEventListener('wheel', (e) => this.wheel(u, e, (v) => this.setWaveView(v)), { passive: false });
					u.over.addEventListener('dblclick', () => this.setWaveView({ min: -DEFAULT_WINDOW / 2, max: DEFAULT_WINDOW / 2 }));
				}],
			},
		}, data, this.waveEl);

		this.keysEl.replaceChildren(...shown.map((s) => el('span', {}, el('span', { class: 'key', style: `background:${s.color}` }), s.label)));
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
		fitHeight();
		this.corr?.setSize({ width: this.corrEl.clientWidth, height: HEIGHT.value });
		this.wave?.setSize({ width: this.waveEl.clientWidth, height: HEIGHT.value });
	}
}
