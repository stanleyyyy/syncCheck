// Window height for sizing charts. On touch devices the browser bars appear and disappear while
// scrolling, which changes the height; there it is only measured again when the width changes.
let width = 0;
let height = 0;

export function viewportHeight() {
	const touch = matchMedia('(pointer: coarse)').matches;
	if (!height || !touch || window.innerWidth !== width) {
		width = window.innerWidth;
		height = window.innerHeight;
	}
	return height;
}

// Resize a uPlot chart only when its size really changes; every resize redraws it.
export function setChartSize(chart, width, height) {
	if (chart && width > 0 && (chart.width !== width || chart.height !== height)) {
		chart.setSize({ width, height });
	}
}
