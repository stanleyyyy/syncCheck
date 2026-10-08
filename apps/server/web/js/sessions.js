// Session library in the sidebar: every measurement with its recording.
import { api } from './api.js';
import { el, formatBytes, formatTime } from './format.js';

function shortDate(started) {
	const date = new Date((started || '').replace(' ', 'T'));
	if (Number.isNaN(date.getTime())) return started || '';
	return date.toLocaleString(undefined, { day: '2-digit', month: 'short', hour: '2-digit', minute: '2-digit' });
}

export class SessionsPanel {
	constructor({ list, search, count, footer, onOpened, onError }) {
		this.list = list;
		this.search = search;
		this.count = count;
		this.footer = footer;
		this.onOpened = onOpened;
		this.onError = onError;
		this.sessions = [];
		this.running = false;
		this.editing = null;		// key of the session being renamed
		this.tagging = null;		// key of the session getting a new tag
		this.confirmDelete = null;
		search.addEventListener('input', () => this.render());
	}

	setRunning(running) {
		this.running = running;
	}

	async refresh() {
		// Do not rebuild the list under an open editor.
		if (this.editing || this.tagging) return;
		try {
			const result = await api.sessions();
			this.sessions = result.sessions;
			const d = result.disk;
			const used = this.sessions.reduce((sum, s) => sum + s.size, 0);
			this.footer.textContent = `${formatBytes(used)} used` + (d.free !== undefined ? ` · ${formatBytes(d.free)} free` : '');
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
		this.editing = this.tagging = null;
		await this.refresh();
	}

	filterByTag(tag) {
		this.search.value = `#${tag}`;
		this.render();
	}

	matches(s, query) {
		if (!query) return true;
		const tags = s.tags || [];
		if (query.startsWith('#')) return tags.some((t) => t.toLowerCase().startsWith(query.slice(1)));
		return [s.name, s.input, s.key, ...tags].some((v) => (v || '').toLowerCase().includes(query));
	}

	render() {
		const query = this.search.value.trim().toLowerCase();
		const shown = this.sessions.filter((s) => this.matches(s, query));
		this.count.textContent = query ? `${shown.length} of ${this.sessions.length}` : `${this.sessions.length}`;
		if (!shown.length) {
			this.list.replaceChildren(el('p', { class: 'muted', style: 'padding: 8px 16px',
				text: this.sessions.length ? 'No session matches.' : 'No sessions yet. Every measurement is stored here with its recording.' }));
			return;
		}
		this.list.replaceChildren(...shown.map((s) => this.renderSession(s)));
		const editor = this.list.querySelector('input');
		if (editor) {
			editor.focus();
			editor.select();
		}
	}

	renderSession(s) {
		const stop = (e) => e.stopPropagation();
		const details = [shortDate(s.started), s.duration ? formatTime(s.duration) : null,
			s.channels ? `${s.channels} ch` : null, s.mode === 'sources' ? 'bands' : null, formatBytes(s.size)].filter(Boolean).join(' · ');

		// Name, renamed in place
		let name;
		if (this.editing === s.key) {
			name = el('input', { type: 'text', value: s.name, onclick: stop,
				onkeydown: (e) => {
					if (e.key === 'Enter') e.target.blur();
					if (e.key === 'Escape') { this.editing = null; this.render(); }
				},
				// Enter and leaving the field both save, once.
				onblur: (e) => {
					if (this.editing !== s.key) return;
					this.editing = null;
					this.run(() => api.renameSession(s.key, e.target.value.trim() || s.name));
				} });
		} else {
			name = el('span', { class: 'name', text: s.name, title: `${s.name} (${s.input || s.key})`,
				ondblclick: (e) => { stop(e); this.editing = s.key; this.render(); } });
		}
		const title = el('div', { class: 'title' }, name,
			this.editing === s.key ? null : el('button', { class: 'edit', title: 'Rename', text: '✎', onclick: (e) => { stop(e); this.editing = s.key; this.render(); } }),
			el('div', { class: 'spacer' }),
			s.running ? el('span', { class: 'badge live', text: 'Recording' }) : null,
			s.status && s.status.startsWith('Error') ? el('span', { class: 'badge', text: 'Error' }) : null);

		// Tags: click filters, × removes, + adds
		const tags = s.tags || [];
		const setTags = (next) => this.run(() => api.tagSession(s.key, next));
		const tagRow = el('div', { class: 'tags' }, ...tags.map((t) => el('span', { class: 'tag', title: `Show sessions tagged ${t}`,
			onclick: (e) => { stop(e); this.filterByTag(t); } }, t,
			el('button', { title: 'Remove tag', text: '×', onclick: (e) => { stop(e); setTags(tags.filter((x) => x !== t)); } }))));
		if (this.tagging === s.key) {
			tagRow.append(el('input', { type: 'text', placeholder: 'tag, …', onclick: stop,
				onkeydown: (e) => {
					if (e.key === 'Enter') {
						const added = e.target.value.split(',').map((t) => t.trim()).filter(Boolean);
						this.tagging = null;
						setTags([...new Set([...tags, ...added])]);
					}
					if (e.key === 'Escape') { this.tagging = null; this.render(); }
				},
				onblur: () => { if (this.tagging === s.key) { this.tagging = null; this.render(); } } }));
		} else {
			tagRow.append(el('button', { class: 'add-tag', text: '+ tag', onclick: (e) => { stop(e); this.tagging = s.key; this.render(); } }));
		}

		// Actions
		const deleting = this.confirmDelete === s.key;
		const buttons = deleting
			? [el('span', { class: 'muted', style: 'font-size:12px', text: s.recorded ? 'Delete with recording?' : 'Delete? (input file is kept)' }),
				el('div', { class: 'spacer' }),
				el('button', { class: 'btn small danger', text: 'Delete', onclick: (e) => { stop(e); this.confirmDelete = null; this.run(() => api.deleteSession(s.key)); } }),
				el('button', { class: 'btn small', text: 'Cancel', onclick: (e) => { stop(e); this.confirmDelete = null; this.render(); } })]
			: [el('a', { class: 'btn small', href: `api/sessions/${s.key}/measurements.csv`, text: 'CSV', onclick: stop }),
				s.audio ? el('a', { class: 'btn small', href: `api/sessions/${s.key}/audio`, text: 'Audio', onclick: stop }) : null,
				el('div', { class: 'spacer' }),
				el('button', { class: 'btn small ghost danger', text: 'Delete', disabled: s.running,
					onclick: (e) => { stop(e); this.confirmDelete = s.key; this.render(); } })];

		return el('div', {
			class: `session${s.current ? ' current' : ''}${deleting ? ' confirming' : ''}`,
			title: s.current ? 'Shown' : this.running ? 'Stop the measurement to open another session' : 'Open this session',
			onclick: () => {
				if (s.current || this.running) return;
				this.run(async () => this.onOpened(await api.openSession(s.key)));
			},
		}, title, el('div', { class: 'details', text: details }), tagRow, el('div', { class: 'buttons' }, ...buttons));
	}
}
