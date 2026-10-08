// Theme handling; chart colors are read from the CSS custom properties.

const listeners = [];

export function cssVar(name) {
	return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

export function seriesColor(index) {
	return cssVar(`--series-${(index % 8) + 1}`);
}

export function chartColors() {
	return { ink: cssVar('--ink'), ink2: cssVar('--ink-2'), muted: cssVar('--muted'), grid: cssVar('--grid'), axis: cssVar('--axis'),
		accent: cssVar('--accent'), surface: cssVar('--surface') };
}

export function onThemeChange(callback) {
	listeners.push(callback);
}

function notify() {
	listeners.forEach((callback) => callback());
}

export function initTheme(button) {
	let stored = null;
	try { stored = localStorage.getItem('theme'); } catch (e) { /* storage unavailable */ }
	if (stored) document.documentElement.dataset.theme = stored;

	matchMedia('(prefers-color-scheme: dark)').addEventListener('change', notify);
	button.addEventListener('click', () => {
		const dark = document.documentElement.dataset.theme
			? document.documentElement.dataset.theme === 'dark'
			: matchMedia('(prefers-color-scheme: dark)').matches;
		const theme = dark ? 'light' : 'dark';
		document.documentElement.dataset.theme = theme;
		try { localStorage.setItem('theme', theme); } catch (e) { /* storage unavailable */ }
		notify();
	});
}
