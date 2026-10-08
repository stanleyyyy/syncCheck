// Inspector: correlation and waveforms at one moment of the session.
import { chartColors, seriesColor } from './theme.js';
import { formatMs, el } from './format.js';

const FONT = '12px system-ui, -apple-system, "Segoe UI", sans-serif';

function axes(colors, xLabel, yLabel, yValues) {
	const common = { stroke: colors.muted, font: FONT, labelFont: FONT, grid: { stroke: colors.grid, width: 1 },
		ticks: { stroke: colors.axis, width: 1, size: 4 } };
	return [
		{ ...common, label: xLabel, labelSize: 18, values: (u, vals) => vals.map((v) => `${v}`) },
		{ ...common, label: yLabel, labelSize: 18, size: 48, values: yValues },
	];
}

export class Inspector {
	constructor({ corrEl, waveEl, tableEl, noAudioEl }) {
		this.corrEl = corrEl;
		this.waveEl = waveEl;
		this.tableEl = tableEl;
		this.noAudioEl = noAudioEl;
		this.result = null;
		this.labels = [];
		this.align = false;
		this.corr = null;
		this.wave = null;
		new ResizeObserver(() => this.resize()).observe(corrEl);
	}

	setPairs(labels) {
		this.labels = labels;
		this.clear();
	}

	setAlign(align) {
		this.align = align;
		if (this.result) this.render(this.result, this.mode);
	}

	clear() {
		this.corr?.destroy();
		this.wave?.destroy();
		this.corr = this.wave = null;
		this.result = null;
		this.tableEl.replaceChildren();
	}

	rebuild() {
		const result = this.result;
		this.clear();
		if (result) this.render(result, this.mode);
	}

	render(result, mode) {
		this.result = result;
		this.mode = mode;
		this.renderCorrelation(result);
		this.renderWaveform(result);
		this.renderTable(result, mode);
	}

	renderCorrelation(result) {
		const colors = chartColors();
		const data = [result.lag, ...result.pairs.map((p) => p.corr)];
		const width = this.corrEl.clientWidth || 500;
		const key = result.pairs.length + ':' + this.labels.join();
		if (!this.corr || this.corrKey !== key) {
			this.corr?.destroy();
			this.corrKey = key;
			this.corr = new uPlot({
				width, height: 240, legend: { show: false },
				series: [{}, ...result.pairs.map((p, i) => ({ label: this.labels[i], stroke: seriesColor(i), width: 2, points: { show: false } }))],
				scales: { x: { time: false }, y: { range: [0, 1.05] } },
				axes: axes(colors, 'Lag (ms)', 'Correlation', (u, vals) => vals.map((v) => v.toFixed(1))),
				cursor: { points: { show: false } },
				hooks: { draw: [(u) => this.drawPeaks(u)] },
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
			return;
		}

		const colors = chartColors();
		const n = wave.channels[0] ? wave.channels[0].min.length : 0;
		const xs = Array.from({ length: n }, (_, i) => +(wave.startMs + i * wave.stepMs).toFixed(4));

		// Channels shown: the compared ones, colored like their pair; the reference in ink.
		const shown = [];
		const ref = result.pairs.length ? result.pairs[0].ref : 0;
		const sameChannel = result.pairs.every((p) => p.ref === p.sig);
		if (sameChannel) {
			shown.push({ channel: ref, label: `Ch ${ref + 1}`, color: colors.ink2, delay: 0 });
		} else {
			shown.push({ channel: ref, label: `Ch ${ref + 1} (reference)`, color: colors.ink2, delay: 0 });
			result.pairs.forEach((p, i) => shown.push({ channel: p.sig, label: `Ch ${p.sig + 1}`, color: seriesColor(i), delay: p.delay }));
		}

		const data = [xs];
		const series = [{}];
		const bands = [];
		for (const s of shown) {
			const trace = wave.channels.find((c) => c.channel === s.channel);
			if (!trace) continue;
			// Align: move a delayed channel back by its measured delay.
			const offset = this.align ? Math.round(s.delay / wave.stepMs) : 0;
			const shift = (values) => values.map((_, i) => {
				const k = i + offset;
				return k >= 0 && k < values.length ? values[k] : null;
			});
			const decimated = wave.stepMs * result.sampleRate / 1000 > 1.5;
			if (decimated) {
				data.push(shift(trace.max), shift(trace.min));
				series.push({ label: s.label, stroke: s.color, width: 1, points: { show: false } },
					{ label: `${s.label} min`, stroke: s.color, width: 1, points: { show: false } });
				bands.push({ series: [series.length - 2, series.length - 1], fill: s.color + '33' });
			} else {
				data.push(shift(trace.max));
				series.push({ label: s.label, stroke: s.color, width: 1.5, points: { show: false } });
			}
		}

		this.wave?.destroy();
		this.wave = new uPlot({
			width: this.waveEl.clientWidth || 500, height: 240, legend: { show: true, live: false }, series, bands,
			scales: { x: { time: false }, y: { range: (u, min, max) => { const m = Math.max(Math.abs(min), Math.abs(max), 1e-3) * 1.1; return [-m, m]; } } },
			axes: axes(colors, 'Time relative to the moment (ms)', 'Amplitude', (u, vals) => vals.map((v) => v.toFixed(2))),
			cursor: { points: { show: false } },
		}, data, this.waveEl);
	}

	renderTable(result, mode) {
		const head = el('tr', {}, el('th', { text: 'Pair' }), el('th', { class: 'right', text: 'Delay at this moment' }),
			el('th', { class: 'right', text: 'PSLR' }));
		const rows = result.pairs.map((p, i) => el('tr', {},
			el('td', {}, el('span', { class: 'row-inline' }, el('span', { class: 'key', style: `background:${seriesColor(i)}` }), this.labels[i] || '')),
			el('td', { class: 'right', text: `${formatMs(p.delay)} ms` }),
			el('td', { class: 'right', text: `${p.pslr.toFixed(1)} dB` })));
		this.tableEl.replaceChildren(el('thead', {}, head), el('tbody', {}, rows));
	}

	resize() {
		const width = this.corrEl.clientWidth;
		if (!width) return;
		this.corr?.setSize({ width, height: 240 });
		this.wave?.setSize({ width: this.waveEl.clientWidth, height: 240 });
	}
}
