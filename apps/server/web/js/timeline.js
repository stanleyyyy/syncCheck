// Session timeline: delay chart, one metric chart and an overview navigator, all on the same time axis.
import { chartColors, seriesColor } from './theme.js';
import { formatTime, formatMs, el } from './format.js';

const FONT = '12px system-ui, -apple-system, "Segoe UI", sans-serif';
const HEIGHT = { delay: 250, metric: 130, overview: 46 };

// The delay chart takes the height the window has left, so the page fits on one screen.
function fitHeights() {
	HEIGHT.delay = Math.max(200, Math.min(520, window.innerHeight - 510));
}

const METRICS = {
	confidence: { label: 'Confidence', unit: '%', format: (v) => (v * 100).toFixed(0), range: [0, 1] },
	pslr: { label: 'PSLR', unit: 'dB', format: (v) => v.toFixed(1) },
	level: { label: 'Level', unit: 'dBFS', format: (v) => v.toFixed(1) },
};

function axes(colors, yLabel, yValues) {
	const common = { stroke: colors.muted, font: FONT, labelFont: FONT, ticks: { stroke: colors.axis, width: 1, size: 4 } };
	return [
		{ ...common, grid: { stroke: colors.grid, width: 1 }, space: 90,
			// Show fractions of a second once the ticks are less than a second apart.
			values: (u, vals, axis, space, incr) => vals.map((v) => formatTime(v, incr < 1)) },
		// As many decimals as the tick spacing needs (at least 2), so close values stay distinguishable.
		{ ...common, grid: { stroke: colors.grid, width: 1 }, label: yLabel, size: 64, labelSize: 18,
			values: (u, vals, axis, space, incr) => vals.map((v) => yValues(v, Math.max(2, Math.min(4, -Math.floor(Math.log10(incr || 1)))))) },
	];
}

// Tooltip listing every series at the cursor; only on the chart under the mouse.
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
		this.store = null;
		this.metric = 'confidence';
		this.rangeSeconds = 600;
		this.follow = true;
		this.xRange = null;
		this.selection = null;
		this.playhead = null;
		this.frozenY = null;
		this.unfreezeTimer = null;
		this.charts = [];
		new ResizeObserver(() => this.resize()).observe(delayEl);
		window.addEventListener('resize', () => this.resize());
	}

	setPairs(labels) {
		this.labels = labels;
		this.selection = null;
		this.playhead = null;
		this.xRange = null;
		this.build();
	}

	build() {
		fitHeights();
		this.charts.forEach((chart) => chart.destroy());
		this.charts = [];
		if (!this.labels.length) return;

		const colors = chartColors();
		const width = this.delayEl.clientWidth || 800;
		const sync = { key: 'timeline', setSeries: false };
		const markers = { hooks: { draw: [(u) => this.drawMarkers(u, colors)] } };
		const cursor = (size) => ({ sync, y: false, drag: { x: false, y: false }, bind: { dblclick: () => null }, points: { size } });

		this.delay = new uPlot({
			width, height: HEIGHT.delay, legend: { show: false },
			series: [{ label: 'Time' }, ...this.labels.map((label, i) => ({ label, stroke: seriesColor(i), width: 2, points: { show: false } }))],
			// At least 0.1 ms tall, so a steady delay is a flat line instead of magnified noise.
			scales: { x: { time: false }, y: { range: (u, min, max) => this.yRange(u, min, max, 0.1) } },
			axes: axes(colors, 'Delay (ms)', (v, decimals) => formatMs(v, decimals)),
			cursor: cursor(8),
			plugins: [tooltipPlugin((v) => formatMs(v), 'ms'), markers],
			hooks: { ready: [(u) => this.bindInteraction(u)] },
		}, this.chartData('delay'), this.delayEl);

		const metric = METRICS[this.metric];
		this.metricChart = new uPlot({
			width, height: HEIGHT.metric, legend: { show: false }, series: this.metricSeries(),
			scales: { x: { time: false }, y: { range: metric.range ? metric.range : (u, min, max) => this.yRange(u, min, max) } },
			axes: axes(colors, `${metric.label} (${metric.unit})`, (v) => metric.format(v)),
			cursor: cursor(6),
			plugins: [tooltipPlugin(metric.format, metric.unit), markers],
			hooks: { ready: [(u) => this.bindInteraction(u)] },
		}, this.chartData(this.metric), this.metricEl);

		const [overviewX, overviewY] = axes(colors, '', () => '');
		this.overview = new uPlot({
			width, height: HEIGHT.overview, legend: { show: false },
			series: [{}, ...this.labels.map((label, i) => ({ stroke: seriesColor(i), width: 1, points: { show: false } }))],
			// Without data uPlot has no x range; fall back to an empty 10 s axis.
			scales: { x: { time: false, range: (u, min, max) => (min === null || max === null || min === max ? [0, 10] : [min, max]) } },
			// The hidden y axis keeps the plot area aligned with the charts above.
			axes: [{ ...overviewX, size: 22 }, { ...overviewY, label: '', values: (u, vals) => vals.map(() => ''), grid: { show: false }, ticks: { show: false } }],
			cursor: { drag: { x: false, y: false }, bind: { dblclick: () => null }, points: { show: false }, y: false },
			hooks: {
				draw: [(u) => this.drawWindow(u, colors), (u) => this.drawMarkers(u, colors)],
				ready: [(u) => this.bindOverview(u)],
			},
		}, this.chartData('delay'), this.overviewEl);

		this.charts = [this.delay, this.metricChart, this.overview];
		this.applyRange();
	}

	// The y axes fit the visible data, except while dragging or zooming, which would make them jump.
	yRange(u, min, max, minSpan = 0) {
		const frozen = this.frozenY && this.frozenY.get(u);
		if (frozen) return frozen;
		if (min === null || max === null) return [0, 1];
		if (max - min < minSpan) {
			const center = (min + max) / 2;
			return [center - minSpan / 2, center + minSpan / 2];
		}
		return uPlot.rangeNum(min, max, 0.1, true);
	}

	freezeY() {
		if (this.frozenY) return;
		this.frozenY = new Map([this.delay, this.metricChart].map((u) => [u, [u.scales.y.min, u.scales.y.max]]));
	}

	unfreezeY() {
		clearTimeout(this.unfreezeTimer);
		this.frozenY = null;
		this.applyRange();
	}

	// Wheel zooming keeps the y axes until the wheel has been idle for a moment.
	freezeYBriefly() {
		this.freezeY();
		clearTimeout(this.unfreezeTimer);
		this.unfreezeTimer = setTimeout(() => this.unfreezeY(), 400);
	}

	metricSeries() {
		const series = [{ label: 'Time' }, ...this.labels.map((label, i) => ({
			label: this.metric === 'level' ? label.replace(/ vs .*/, '') : label,
			stroke: seriesColor(i), width: 2, points: { show: false },
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

	dataExtent() {
		const t = this.store ? this.store.t : [];
		return t.length ? { min: t[0], max: Math.max(t[t.length - 1], t[0] + 1) } : null;
	}

	// Current x range: following the end of the data, or what the user picked.
	currentRange() {
		const extent = this.dataExtent();
		if (!extent) return { min: 0, max: Math.max(this.rangeSeconds, 10) };
		if (!this.follow && this.xRange) return this.xRange;
		if (!this.rangeSeconds) return extent;
		return { min: Math.max(extent.min, extent.max - this.rangeSeconds), max: Math.max(extent.max, extent.min + 1) };
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
		const extent = this.dataExtent();
		if (!extent || max - min <= 0) return;
		// Zoomed out to everything: show all and keep following.
		if (max - min >= extent.max - extent.min) {
			this.showAll();
			return;
		}
		const width = max - min;
		if (min < extent.min) { min = extent.min; max = min + width; }
		if (max > extent.max) { max = extent.max; min = max - width; }
		this.xRange = { min, max };
		if (this.follow) {
			this.follow = false;
			this.onFollowChange(false);
		}
		this.applyRange();
	}

	// Zoom by factor (>1 zooms out) around time 'at'.
	zoom(factor, at) {
		const { min, max } = this.currentRange();
		const center = at === undefined || at === null ? (min + max) / 2 : at;
		const width = Math.max((max - min) * factor, 0.5);
		const ratio = max > min ? (center - min) / (max - min) : 0.5;
		this.setXRange(center - width * ratio, center + width * (1 - ratio));
	}

	pan(fraction) {
		const { min, max } = this.currentRange();
		const shift = (max - min) * fraction;
		this.setXRange(min + shift, max + shift);
	}

	// Ctrl+wheel (or pinch) zooms around the mouse, a horizontal swipe moves; the plain wheel scrolls the page.
	onWheel(u, e) {
		const horizontal = Math.abs(e.deltaX) > Math.abs(e.deltaY);
		if (!(e.ctrlKey || e.metaKey) && !horizontal) return;
		e.preventDefault();
		this.freezeYBriefly();
		if (horizontal) {
			this.pan(e.deltaX / Math.max(u.over.clientWidth, 1));
		} else {
			this.zoom(Math.exp(e.deltaY * 0.002), u.posToVal(e.offsetX, 'x'));
		}
	}

	// Drag moves the view, a click (without moving) selects the moment under the mouse.
	bindInteraction(u) {
		u.over.addEventListener('mousedown', (e) => {
			if (e.button !== 0) return;
			const start = { x: e.clientX, range: this.currentRange(), moved: false };
			const pxToTime = (start.range.max - start.range.min) / Math.max(u.over.clientWidth, 1);
			const move = (m) => {
				const dx = m.clientX - start.x;
				if (!start.moved && Math.abs(dx) < 4) return;
				if (!start.moved) this.freezeY();
				start.moved = true;
				u.over.classList.add('dragging');
				this.setXRange(start.range.min - dx * pxToTime, start.range.max - dx * pxToTime);
			};
			const up = () => {
				window.removeEventListener('mousemove', move);
				window.removeEventListener('mouseup', up);
				u.over.classList.remove('dragging');
				if (start.moved) {
					this.unfreezeY();
				} else if (u.cursor.idx !== null && u.cursor.idx !== undefined) {
					this.onSelect(u.data[0][u.cursor.idx]);
				}
			};
			window.addEventListener('mousemove', move);
			window.addEventListener('mouseup', up);
			e.preventDefault();
		});
		u.over.addEventListener('dblclick', () => this.showAll());
		u.over.addEventListener('wheel', (e) => this.onWheel(u, e), { passive: false });
	}

	// Overview: press or drag to move the view to the mouse, wheel moves the view.
	bindOverview(u) {
		const center = (clientX) => {
			const { min, max } = this.currentRange();
			const time = u.posToVal(clientX - u.over.getBoundingClientRect().left, 'x');
			this.setXRange(time - (max - min) / 2, time + (max - min) / 2);
		};
		u.over.addEventListener('mousedown', (e) => {
			if (e.button !== 0) return;
			this.freezeY();
			center(e.clientX);
			u.over.classList.add('dragging');
			const move = (m) => center(m.clientX);
			const up = () => {
				window.removeEventListener('mousemove', move);
				window.removeEventListener('mouseup', up);
				u.over.classList.remove('dragging');
				this.unfreezeY();
			};
			window.addEventListener('mousemove', move);
			window.addEventListener('mouseup', up);
			e.preventDefault();
		});
		u.over.addEventListener('dblclick', () => this.showAll());
		u.over.addEventListener('wheel', (e) => this.onWheel(u, e), { passive: false });
	}

	setSelection(time) {
		this.selection = time;
		this.redrawMarkers();
	}

	setPlayhead(time) {
		this.playhead = time;
		this.redrawMarkers();
	}

	redrawMarkers() {
		this.charts.forEach((chart) => chart.redraw(false, false));
	}

	drawLine(u, time, color, dash) {
		if (time === null) return;
		const x = u.valToPos(time, 'x', true);
		if (x < u.bbox.left || x > u.bbox.left + u.bbox.width) return;
		const ctx = u.ctx;
		ctx.save();
		ctx.strokeStyle = color;
		ctx.lineWidth = 1.5 * devicePixelRatio;
		ctx.setLineDash(dash.map((d) => d * devicePixelRatio));
		ctx.beginPath();
		ctx.moveTo(x, u.bbox.top);
		ctx.lineTo(x, u.bbox.top + u.bbox.height);
		ctx.stroke();
		ctx.restore();
	}

	// Selected moment (dashed) and playback position (solid).
	drawMarkers(u, colors) {
		this.drawLine(u, this.selection, colors.ink2, [4, 3]);
		this.drawLine(u, this.playhead, colors.accent, []);
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
		fitHeights();
		this.delay?.setSize({ width, height: HEIGHT.delay });
		this.metricChart?.setSize({ width, height: HEIGHT.metric });
		this.overview?.setSize({ width, height: HEIGHT.overview });
	}
}
