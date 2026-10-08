// Vertical scaling of a chart: 'view' fits the visible data, 'recording' the data of the whole
// recording (so it does not jump while moving through it), 'manual' is set on the vertical axis:
// scrolling there zooms around the pointer, dragging it moves. The choice is remembered per chart.

export class VerticalScale {
	// controls: the element holding the mode buttons (data-scale), a hint and a fit button (data-fit).
	constructor(controls, storageKey, onChange) {
		this.controls = controls;
		this.storageKey = storageKey;
		this.onChange = onChange;
		this.mode = 'recording';
		try { this.mode = localStorage.getItem(storageKey) || 'recording'; } catch (e) { /* storage unavailable */ }
		this.manual = null;
		this.chart = null;
		controls.classList.add('vscale');
		controls.innerHTML = verticalScaleControls();
		controls.addEventListener('click', (e) => {
			if (e.target.dataset.scale) this.setMode(e.target.dataset.scale);
			if (e.target.dataset.fit !== undefined) this.fitManual();
		});
		this.updateControls();
	}

	// The y range for the chart's range function; fit() is the range of the visible data,
	// whole() that of the whole recording (or null when unknown).
	range(fit, whole) {
		if (this.mode === 'manual' && this.manual) return this.manual;
		if (this.mode === 'recording') {
			const range = whole();
			if (range) return range;
		}
		return fit();
	}

	setMode(mode) {
		if (mode === 'manual' && !this.manual && this.chart) this.manual = [this.chart.scales.y.min, this.chart.scales.y.max];
		this.mode = mode;
		try { localStorage.setItem(this.storageKey, mode); } catch (e) { /* storage unavailable */ }
		this.updateControls();
		this.onChange();
	}

	// Manual range that fits the visible data, as a starting point.
	fitManual() {
		this.manual = null;
		this.mode = 'view';
		this.onChange();
		// The chart applies the new range after this task; take it over then.
		requestAnimationFrame(() => this.setMode('manual'));
	}

	updateControls() {
		for (const button of this.controls.querySelectorAll('[data-scale]')) button.classList.toggle('on', button.dataset.scale === this.mode);
		this.hint?.classList.toggle('hidden', this.mode !== 'manual');
	}

	setManual(range) {
		this.manual = range;
		if (this.mode !== 'manual') {
			this.mode = 'manual';
			try { localStorage.setItem(this.storageKey, 'manual'); } catch (e) { /* storage unavailable */ }
			this.updateControls();
		}
		this.chart.setScale('y', { min: range[0], max: range[1] });
	}

	// Wheel and drag on the chart's vertical axis.
	bindAxis(u) {
		this.chart = u;
		const axis = u.root.querySelectorAll('.u-axis')[1];
		if (!axis) return;
		axis.classList.add('scale-axis');
		axis.title = 'Scroll to zoom, drag to move the vertical scale';
		// In manual mode a note on the chart explains the axis; it covers the chart, so nothing moves.
		this.hint = document.createElement('div');
		this.hint.className = 'scale-hint';
		this.hint.textContent = 'Manual scale: scroll or drag the vertical axis';
		u.over.append(this.hint);
		this.updateControls();
		const current = () => [u.scales.y.min, u.scales.y.max];
		axis.addEventListener('wheel', (e) => {
			e.preventDefault();
			const [min, max] = current();
			const at = u.posToVal(e.clientY - u.over.getBoundingClientRect().top, 'y');
			const factor = Math.exp(e.deltaY * 0.002);
			this.setManual([at - (at - min) * factor, at + (max - at) * factor]);
		}, { passive: false });
		let drag = null;
		axis.addEventListener('pointerdown', (e) => {
			axis.setPointerCapture(e.pointerId);
			drag = { y: e.clientY, range: current() };
			e.preventDefault();
		});
		axis.addEventListener('pointermove', (e) => {
			if (!drag) return;
			const [min, max] = drag.range;
			const shift = (e.clientY - drag.y) * (max - min) / Math.max(u.over.clientHeight, 1);
			this.setManual([min + shift, max + shift]);
		});
		const end = () => { drag = null; };
		axis.addEventListener('pointerup', end);
		axis.addEventListener('pointercancel', end);
	}
}

// Markup of the controls: label, mode buttons, and (shown in manual mode) a hint and a fit button.
export function verticalScaleControls() {
	return `<span class="muted">Vertical scaling</span>
		<div class="seg small">
			<button data-scale="view" title="Fit the visible data">View</button><button data-scale="recording" title="Fit the whole recording, so the scale does not change while moving through it">Recording</button><button data-scale="manual" title="Set the scale on the vertical axis">Manual</button>
		</div>
		<div class="seg small"><button data-fit title="Manual scale that fits the visible data">Fit</button></div>`;
}
