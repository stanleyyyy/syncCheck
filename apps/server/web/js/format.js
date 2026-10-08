// Formatting helpers and pair labels.

export function formatTime(seconds, withMs = false) {
	if (!Number.isFinite(seconds)) return '';
	const sign = seconds < 0 ? '-' : '';
	seconds = Math.abs(seconds);
	const h = Math.floor(seconds / 3600);
	const m = Math.floor((seconds % 3600) / 60);
	const s = seconds % 60;
	const ss = withMs ? s.toFixed(2).padStart(5, '0') : String(Math.floor(s)).padStart(2, '0');
	return sign + (h ? `${h}:${String(m).padStart(2, '0')}:${ss}` : `${m}:${ss}`);
}

export function formatMs(value, digits = 3) {
	if (!Number.isFinite(value)) return '–';
	return (value > 0 ? '+' : value < 0 ? '−' : '') + Math.abs(value).toFixed(digits);
}

// Channels are shown 1-based, bands 0-based (band 0 is the reference band of the band test signal).
export function pairLabel(pair, mode) {
	if (mode === 'channels' || (pair.refBand === 0 && pair.sigBand === 0)) {
		return `Ch ${pair.sig + 1} vs Ch ${pair.ref + 1}`;
	}
	if (pair.ref === pair.sig) {
		return `Band ${pair.sigBand} vs Band ${pair.refBand}`;
	}
	return `Ch ${pair.sig + 1}·B${pair.sigBand} vs Ch ${pair.ref + 1}·B${pair.refBand}`;
}

// Same thresholds as the command line tool (PSLR) combined with the confidence.
export function quality(pslr, confidence) {
	if (pslr > 5 && confidence > 0.8) return { key: 'good', label: 'Good' };
	if (pslr > 2 && confidence > 0.5) return { key: 'fair', label: 'Fair' };
	return { key: 'poor', label: 'Poor' };
}

// Least-squares slope of y over x, in y units per x unit.
export function slope(xs, ys, from, to) {
	let n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
	for (let i = from; i < to; i++) {
		const x = xs[i], y = ys[i];
		if (!Number.isFinite(y)) continue;
		n++; sx += x; sy += y; sxx += x * x; sxy += x * y;
	}
	const d = n * sxx - sx * sx;
	return n > 2 && d !== 0 ? (n * sxy - sx * sy) / d : NaN;
}

export function el(tag, attrs = {}, ...children) {
	const node = document.createElement(tag);
	for (const [key, value] of Object.entries(attrs)) {
		if (value === undefined || value === null || value === false) continue;
		if (key === 'class') node.className = value;
		else if (key === 'text') node.textContent = value;
		else if (key.startsWith('on')) node.addEventListener(key.slice(2), value);
		else if (key in node && typeof value !== 'string') node[key] = value;
		else node.setAttribute(key, value === true ? '' : value);
	}
	for (const child of children.flat()) {
		if (child !== null && child !== undefined) node.append(child);
	}
	return node;
}
