// Session timeline: delay chart, one metric chart and an overview navigator, all on the same time axis.
import { chartColors, seriesColor } from './theme.js';
import { formatTime, formatMs, el } from './format.js';

const FONT = '12px system-ui, -apple-system, "Segoe UI", sans-serif';

const METRICS = {
	confidence: { label: 'Confidence', unit: '%', format: (v) => (v * 100).toFixed(0), range: [0, 1] },
	pslr: { label: 'PSLR', unit: 'dB', format: (v) => v.toFixed(1) },
	level: { label: 'Level', unit: 'dBFS', format: (v) => v.toFixed(1) },
};

function axes(colors, yLabel, yValues, showX = true) {
	const common = { stroke: colors.muted, font: FONT, labelFont: FONT, ticks: { stroke: colors.axis, width: 1, size: 4 } };
	return [
		{ ...common, grid: { stroke: colors.grid, width: 1 }, space: 90, show: showX,
			values: (u, vals) => vals.map((v) => formatTime(v)) },
		{ ...common, grid: { stroke: colors.grid, width: 1 }, label: yLabel, size: 64, labelSize: 18,
			values: (u, vals) => vals.map(yValues) },
	];
}

// Tooltip listing every visible series at the cursor.
function tooltipPlugin(format, unit) {
	let tip;
	return {
		hooks: {
			init: (u) => {
				tip = el('div', { class: 'tooltip hidden' });
				u.over.append(tip);
				u.over.addEventListener('mouseleave', () => tip.classList.add('hidden'));
			},
			setCursor: (u) => {
				// Synced charts move their cursor too; only the chart under the mouse shows a tooltip.
				const idx = u.cursor.idx;
				if (idx === null || idx === undefined || u.cursor.left < 0 || !u.over.matches(':hover')) {
					tip.classList.add('hidden');
					return;
				}
				tip.replaceChildren(el('div', { class: 'time', text: formatTime(u.data[0][idx], true) }));
				u.series.forEach((s, i) => {
					if (i === 0 || !s.show) return;
					const value = u.data[i][idx];
					tip.append(el('div', { class: 'row' },
						el('span', { class: 'key', style: `background:${s._stroke}` }),
						el('strong', { text: Number.isFinite(value) ? `${format(value)} ${unit}` : '–' }),
						el('span', { text: s.label })));
				});
				tip.classList.remove('hidden');
				const width = u.over.clientWidth;
				const left = u.cursor.left + 14;
				tip.style.left = `${left + tip.offsetWidth > width ? u.cursor.left - tip.offsetWidth - 14 : left}px`;
				tip.style.top = `${Math.max(0, u.cursor.top - 20)}px`;
			},
		},
	};
}

export class Timeline {
	constructor({ delayEl, metricEl, overviewEl, onSelect, onFollowChange }) {
		this.delayEl = delayEl;
		this.metricEl = metricEl;
		this.overviewEl = overviewEl;
		this.onSelect = onSelect;
		this.onFollowChange = onFollowChange;
		this.labels = [];
		this.visible = [];
		this.store = null;
		this.metric = 'confidence';
		this.rangeSeconds = 600;
		this.follow = true;
		this.xRange = null;
		this.selection = null;
		this.charts = [];
		new ResizeObserver(() => this.resize()).observe(delayEl);
	}

	setPairs(labels) {
		this.labels = labels;
		this.visible = labels.map(() => true);
		this.selection = null;
		this.xRange = null;
		this.build();
	}

	build() {
		this.charts.forEach((chart) => chart.destroy());
		this.charts = [];
		if (!this.labels.length) return;

		const colors = chartColors();
		const width = this.delayEl.clientWidth || 800;
		const series = (fill = false) => [{ label: 'Time' }, ...this.labels.map((label, i) => ({
			label, stroke: seriesColor(i), width: 2, show: this.visible[i], points: { show: false },
			fill: fill ? undefined : undefined,
		}))];
		const sync = { key: 'timeline', setSeries: false };
		const marker = { hooks: { draw: [(u) => this.drawSelection(u, colors)] } };
		const interaction = (u) => this.bindInteraction(u);

		this.delay = new uPlot({
			width, height: 280, legend: { show: false }, series: series(),
			scales: { x: { time: false } },
			axes: axes(colors, 'Delay (ms)', (v) => formatMs(v, 2)),
			cursor: { sync, y: false, drag: { x: true, y: false, setScale: false }, bind: { dblclick: () => null }, points: { size: 8 } },
			plugins: [tooltipPlugin((v) => formatMs(v), 'ms'), marker],
			hooks: { ready: [interaction], setSelect: [(u) => this.onDragSelect(u)] },
		}, this.chartData('delay'), this.delayEl);

		const metric = METRICS[this.metric];
		this.metricChart = new uPlot({
			width, height: 140, legend: { show: false }, series: this.metricSeries(),
			scales: { x: { time: false }, y: metric.range ? { range: metric.range } : {} },
			axes: axes(colors, `${metric.label} (${metric.unit})`, (v) => metric.format(v)),
			cursor: { sync, y: false, drag: { x: true, y: false, setScale: false }, bind: { dblclick: () => null }, points: { size: 6 } },
			plugins: [tooltipPlugin(metric.format, metric.unit), marker],
			hooks: { ready: [interaction], setSelect: [(u) => this.onDragSelect(u)] },
		}, this.chartData(this.metric), this.metricEl);

		this.overview = new uPlot({
			width, height: 70, legend: { show: false },
			series: [{}, ...this.labels.map((label, i) => ({ stroke: seriesColor(i), width: 1, show: this.visible[i], points: { show: false } }))],
			// Without data uPlot has no x range; fall back to an empty 10 s axis.
			scales: { x: { time: false, range: (u, min, max) => (min === null || max === null || min === max ? [0, 10] : [min, max]) } },
			axes: (() => {
				const [x, y] = axes(colors, '', () => '');
				// The hidden y axis keeps the plot area aligned with the charts above.
				return [{ ...x, size: 24 }, { ...y, label: '', values: (u, vals) => vals.map(() => ''), grid: { show: false }, ticks: { show: false } }];
			})(),
			cursor: { drag: { x: true, y: false, setScale: false }, bind: { dblclick: () => null }, points: { show: false }, y: false },
			hooks: {
				draw: [(u) => this.drawWindow(u, colors)],
				setSelect: [(u) => this.onOverviewSelect(u)],
				ready: [(u) => u.over.addEventListener('dblclick', () => this.showAll())],
			},
		}, this.chartData('delay'), this.overviewEl);

		this.charts = [this.delay, this.metricChart, this.overview];
		this.applyRange();
	}

	metricSeries() {
		const series = [{ label: 'Time' }, ...this.labels.map((label, i) => ({
			label: this.metric === 'level' ? label.replace(/ vs .*/, '') : label,
			stroke: seriesColor(i), width: 2, show: this.visible[i], points: { show: false },
		}))];
		if (this.metric === 'level') {
			const ref = this.labels.length ? this.labels[0].replace(/.* vs /, '') : 'Reference';
			series.push({ label: `${ref} (reference)`, stroke: chartColors().ink2, width: 1.5, dash: [4, 4], points: { show: false } });
		}
		return series;
	}

	chartData(kind) {
		const store = this.store;
		if (!store) return [[], ...this.labels.map(() => [])];
		if (kind === 'level') {
			return [store.t, ...store.pairs.map((p) => p.sigRms), store.pairs[0] ? store.pairs[0].refRms : []];
		}
		return [store.t, ...store.pairs.map((p) => p[kind])];
	}

	setData(store) {
		this.store = store;
		if (!this.charts.length) return;
		this.delay.setData(this.chartData('delay'), false);
		this.metricChart.setData(this.chartData(this.metric), false);
		this.overview.setData(this.chartData('delay'), true);
		this.applyRange();
	}

	setMetric(metric) {
		this.metric = metric;
		this.build();
	}

	setVisible(index, show) {
		this.visible[index] = show;
		this.delay?.setSeries(index + 1, { show });
		this.metricChart?.setSeries(index + 1, { show });
		this.overview?.setSeries(index + 1, { show });
	}

	setRangeSeconds(seconds) {
		this.rangeSeconds = seconds;
		this.setFollow(true);
	}

	setFollow(follow) {
		this.follow = follow;
		if (follow) this.xRange = null;
		this.onFollowChange(follow);
		this.applyRange();
	}

	showAll() {
		this.rangeSeconds = 0;
		this.setFollow(true);
	}

	// Current x range: following the end of the data, or what the user picked.
	currentRange() {
		const t = this.store ? this.store.t : [];
		if (!t.length) return { min: 0, max: Math.max(this.rangeSeconds, 10) };
		const first = t[0], last = t[t.length - 1];
		if (!this.follow && this.xRange) return this.xRange;
		if (!this.rangeSeconds) return { min: first, max: Math.max(last, first + 1) };
		return { min: Math.max(first, last - this.rangeSeconds), max: Math.max(last, first + Math.min(this.rangeSeconds, 1)) };
	}

	visibleIndexRange() {
		const t = this.store ? this.store.t : [];
		const { min, max } = this.currentRange();
		let from = 0;
		while (from < t.length && t[from] < min) from++;
		let to = from;
		while (to < t.length && t[to] <= max) to++;
		return [from, to];
	}

	applyRange() {
		if (!this.charts.length) return;
		const range = this.currentRange();
		this.delay.setScale('x', range);
		this.metricChart.setScale('x', range);
		this.overview.redraw(false, false);
	}

	setXRange(min, max) {
		if (max - min <= 0) return;
		this.xRange = { min, max };
		if (this.follow) {
			this.follow = false;
			this.onFollowChange(false);
		}
		this.applyRange();
	}

	onDragSelect(u) {
		if (u.select.width > 4) {
			this.setXRange(u.posToVal(u.select.left, 'x'), u.posToVal(u.select.left + u.select.width, 'x'));
		}
		u.setSelect({ left: 0, width: 0, top: 0, height: 0 }, false);
	}

	onOverviewSelect(u) {
		if (u.select.width > 4) {
			this.setXRange(u.posToVal(u.select.left, 'x'), u.posToVal(u.select.left + u.select.width, 'x'));
		}
		u.setSelect({ left: 0, width: 0, top: 0, height: 0 }, false);
	}

	bindInteraction(u) {
		let down = null;
		u.over.addEventListener('mousedown', (e) => { down = { x: e.clientX, y: e.clientY }; });
		u.over.addEventListener('mouseup', (e) => {
			if (down && Math.abs(e.clientX - down.x) < 4 && Math.abs(e.clientY - down.y) < 4 && u.cursor.idx !== null) {
				this.onSelect(u.data[0][u.cursor.idx]);
			}
			down = null;
		});
		u.over.addEventListener('dblclick', () => this.showAll());
	}

	setSelection(time) {
		this.selection = time;
		this.delay?.redraw(false, false);
		this.metricChart?.redraw(false, false);
	}

	drawSelection(u, colors) {
		if (this.selection === null) return;
		const x = u.valToPos(this.selection, 'x', true);
		if (x < u.bbox.left || x > u.bbox.left + u.bbox.width) return;
		const ctx = u.ctx;
		ctx.save();
		ctx.strokeStyle = colors.ink2;
		ctx.lineWidth = 1.5 * devicePixelRatio;
		ctx.setLineDash([4 * devicePixelRatio, 3 * devicePixelRatio]);
		ctx.beginPath();
		ctx.moveTo(x, u.bbox.top);
		ctx.lineTo(x, u.bbox.top + u.bbox.height);
		ctx.stroke();
		ctx.restore();
	}

	// Shade the parts of the overview outside the visible window.
	drawWindow(u, colors) {
		if (!this.store || !this.store.t.length) return;
		const { min, max } = this.currentRange();
		const left = Math.max(u.bbox.left, u.valToPos(min, 'x', true));
		const right = Math.min(u.bbox.left + u.bbox.width, u.valToPos(max, 'x', true));
		const ctx = u.ctx;
		ctx.save();
		ctx.fillStyle = colors.surface;
		ctx.globalAlpha = 0.7;
		ctx.fillRect(u.bbox.left, u.bbox.top, left - u.bbox.left, u.bbox.height);
		ctx.fillRect(right, u.bbox.top, u.bbox.left + u.bbox.width - right, u.bbox.height);
		ctx.globalAlpha = 1;
		ctx.strokeStyle = colors.accent;
		ctx.lineWidth = devicePixelRatio;
		ctx.strokeRect(left, u.bbox.top, right - left, u.bbox.height);
		ctx.restore();
	}

	resize() {
		const width = this.delayEl.clientWidth;
		if (!width) return;
		this.delay?.setSize({ width, height: 280 });
		this.metricChart?.setSize({ width, height: 140 });
		this.overview?.setSize({ width, height: 70 });
	}
}
