// Session library: stored measurements with their recordings.
import { api } from './api.js';
import { el, formatBytes, formatTime } from './format.js';

export class SessionsDrawer {
	constructor({ drawer, scrim, body, footer, onOpened, onError }) {
		this.drawer = drawer;
		this.scrim = scrim;
		this.body = body;
		this.footer = footer;
		this.onOpened = onOpened;
		this.onError = onError;
		this.sessions = [];
		this.confirmDelete = null;
	}

	async open() {
		this.drawer.classList.remove('hidden');
		this.scrim.classList.remove('hidden');
		await this.refresh();
	}

	close() {
		this.drawer.classList.add('hidden');
		this.scrim.classList.add('hidden');
	}

	isOpen() {
		return !this.drawer.classList.contains('hidden');
	}

	async refresh() {
		try {
			const result = await api.sessions();
			this.sessions = result.sessions;
			const d = result.disk;
			this.footer.textContent = d.free !== undefined
				? `${formatBytes(this.sessions.reduce((sum, s) => sum + s.size, 0))} in ${this.sessions.length} sessions · ${formatBytes(d.free)} free`
				: '';
		} catch (e) {
			this.onError(e.message);
		}
		this.render();
	}

	async run(action) {
		try {
			await action();
		} catch (e) {
			this.onError(e.message);
		}
		await this.refresh();
	}

	render() {
		if (!this.sessions.length) {
			this.body.replaceChildren(el('p', { class: 'muted', text: 'No sessions yet. Every measurement is stored here with its recording.' }));
			return;
		}
		this.body.replaceChildren(...this.sessions.map((s) => {
			const details = [s.started, s.duration ? formatTime(s.duration) : null, s.input,
				s.channels ? `${s.channels} ch` : null, s.mode === 'sources' ? 'bands' : s.mode ? 'channels' : null, formatBytes(s.size)]
				.filter(Boolean).join(' · ');
			const name = el('input', { type: 'text', value: s.name, title: 'Rename',
				onchange: (e) => this.run(() => api.renameSession(s.key, e.target.value)) });
			const deleting = this.confirmDelete === s.key;
			const buttons = deleting
				? [el('span', { class: 'muted', text: s.recorded ? 'Delete the session and its recording?' : 'Delete the session? (the input file is kept)' }),
					el('button', { class: 'btn small danger', text: 'Delete', onclick: () => { this.confirmDelete = null; this.run(() => api.deleteSession(s.key)); } }),
					el('button', { class: 'btn small', text: 'Cancel', onclick: () => { this.confirmDelete = null; this.render(); } })]
				: [el('button', { class: 'btn small', text: s.current ? 'Shown' : 'Open', disabled: s.current,
					onclick: () => this.run(async () => { this.onOpened(await api.openSession(s.key)); this.close(); }) }),
					el('a', { class: 'btn small', href: `api/sessions/${s.key}/measurements.csv`, text: 'CSV' }),
					s.audio ? el('a', { class: 'btn small', href: `api/sessions/${s.key}/audio`, text: 'Audio' }) : null,
					el('div', { class: 'spacer' }),
					el('button', { class: 'btn small ghost danger', text: 'Delete', disabled: s.running,
						onclick: () => { this.confirmDelete = s.key; this.render(); } })];
			return el('div', { class: `session${s.current ? ' current' : ''}` },
				el('div', { class: 'title' }, name,
					s.running ? el('span', { class: 'badge live', text: 'Recording' }) : s.current ? el('span', { class: 'badge', text: 'Shown' }) : null,
					s.status && s.status.startsWith('Error') ? el('span', { class: 'badge', text: 'Error' }) : null),
				el('div', { class: 'details', text: details }),
				el('div', { class: 'buttons' }, ...buttons),
				el('div', { class: 'id', text: s.key }));
		}));
	}
}
