// Session library in the sidebar: every measurement with its recording.
import { api } from './api.js';
import { confirmAction } from './confirm.js';
import { el, formatBytes, formatTime } from './format.js';

function shortDate(started) {
	const date = new Date((started || '').replace(' ', 'T'));
	if (Number.isNaN(date.getTime())) return started || '';
	return date.toLocaleString(undefined, { day: '2-digit', month: 'short', hour: '2-digit', minute: '2-digit' });
}

function download(href) {
	const link = el('a', { href, download: '' });
	document.body.append(link);
	link.click();
	link.remove();
}

export class SessionsPanel {
	constructor({ list, search, count, footer, selectionBar, onOpened, onError }) {
		this.list = list;
		this.search = search;
		this.count = count;
		this.footer = footer;
		this.selectionBar = selectionBar;
		this.onOpened = onOpened;
		this.onError = onError;
		this.sessions = [];
		this.running = false;
		this.selected = new Set();
		this.editing = null;	// key of the session being renamed
		this.tagging = null;	// key of the session getting a new tag
		this.menu = null;		// key of the session whose menu is open
		search.addEventListener('input', () => this.render());
		document.addEventListener('click', () => { if (this.menu) { this.menu = null; this.render(); } });
	}

	setRunning(running) {
		this.running = running;
	}

	async refresh() {
		// Do not rebuild the list under an open editor or menu.
		if (this.editing || this.tagging || this.menu) return;
		try {
			const result = await api.sessions();
			this.sessions = result.sessions;
			const keys = new Set(this.sessions.map((s) => s.key));
			this.selected = new Set([...this.selected].filter((k) => keys.has(k)));
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
		this.editing = this.tagging = this.menu = null;
		await this.refresh();
	}

	visibleSessions() {
		const query = this.search.value.trim().toLowerCase();
		if (!query) return this.sessions;
		return this.sessions.filter((s) => {
			const tags = s.tags || [];
			if (query.startsWith('#')) return tags.some((t) => t.toLowerCase().startsWith(query.slice(1)));
			return [s.name, s.input, s.key, ...tags].some((v) => (v || '').toLowerCase().includes(query));
		});
	}

	async deleteSessions(list) {
		if (!list.length) return;
		const recorded = list.some((s) => s.recorded);
		const ok = await confirmAction({
			title: list.length === 1 ? `Delete “${list[0].name}”?` : `Delete ${list.length} sessions?`,
			message: `The measurements${recorded ? ' and recordings' : ''} are deleted permanently (${formatBytes(list.reduce((n, s) => n + s.size, 0))}).`
				+ (list.some((s) => !s.recorded && s.source === 'file') ? ' Input files that were analyzed are kept.' : ''),
		});
		if (!ok) return;
		await this.run(async () => {
			for (const s of list) {
				await api.deleteSession(s.key);
				this.selected.delete(s.key);
			}
		});
	}

	render() {
		const shown = this.visibleSessions();
		const query = this.search.value.trim();
		this.count.textContent = query ? `${shown.length} of ${this.sessions.length}` : `${this.sessions.length}`;
		this.renderSelectionBar(shown);
		if (!shown.length) {
			this.list.replaceChildren(el('p', { class: 'muted', style: 'padding: 8px 16px',
				text: this.sessions.length ? 'No session matches.' : 'No sessions yet. Every measurement is stored here with its recording.' }));
			return;
		}
		this.list.replaceChildren(...shown.map((s) => this.renderSession(s)));
		const editor = this.list.querySelector('.title input, .tags input');
		if (editor) {
			editor.focus();
			editor.select();
		}
	}

	renderSelectionBar(shown) {
		const bar = this.selectionBar;
		bar.classList.toggle('hidden', !this.selected.size);
		if (!this.selected.size) return;
		const selectable = shown.filter((s) => !s.running);
		const chosen = this.sessions.filter((s) => this.selected.has(s.key));
		bar.replaceChildren(
			el('span', { text: `${this.selected.size} selected` }),
			selectable.every((s) => this.selected.has(s.key)) ? null
				: el('button', { class: 'btn small ghost', text: 'All', title: 'Select all listed sessions', onclick: () => { selectable.forEach((s) => this.selected.add(s.key)); this.render(); } }),
			el('div', { class: 'spacer' }),
			el('button', { class: 'btn small primary danger', text: 'Delete…', onclick: () => this.deleteSessions(chosen) }),
			el('button', { class: 'btn small ghost', text: '✕', title: 'Clear the selection', onclick: () => { this.selected.clear(); this.render(); } }));
	}

	renderSession(s) {
		const stop = (e) => e.stopPropagation();
		const details = [shortDate(s.started), s.duration ? formatTime(s.duration) : null,
			s.channels ? `${s.channels} ch` : null, s.mode === 'sources' ? 'bands' : null, formatBytes(s.size)].filter(Boolean).join(' · ');
		const tags = s.tags || [];
		const setTags = (next) => this.run(() => api.tagSession(s.key, next));
		const startRename = () => { this.menu = null; this.editing = s.key; this.render(); };
		const startTagging = () => { this.menu = null; this.tagging = s.key; this.render(); };

		const checkbox = el('input', { type: 'checkbox', checked: this.selected.has(s.key), disabled: s.running,
			title: s.running ? 'The running session cannot be selected' : 'Select', onclick: stop,
			onchange: (e) => { if (e.target.checked) this.selected.add(s.key); else this.selected.delete(s.key); this.render(); } });

		// Name, renamed in place: Enter or leaving the field saves, Esc cancels.
		const name = this.editing === s.key
			? el('input', { type: 'text', value: s.name, onclick: stop,
				onkeydown: (e) => {
					if (e.key === 'Enter') e.target.blur();
					if (e.key === 'Escape') { this.editing = null; this.render(); }
				},
				onblur: (e) => {
					if (this.editing !== s.key) return;
					this.editing = null;
					this.run(() => api.renameSession(s.key, e.target.value.trim() || s.name));
				} })
			: el('span', { class: 'name', text: s.name, title: `${s.name} (${s.input || s.key})`, ondblclick: (e) => { stop(e); startRename(); } });
		const title = el('div', { class: 'title' }, name,
			s.running ? el('span', { class: 'badge live', text: 'Recording' }) : null,
			s.status && s.status.startsWith('Error') ? el('span', { class: 'badge', text: 'Error' }) : null);

		// Tags: click filters, × removes, + adds (comma separated)
		const tagRow = el('div', { class: 'tags' }, ...tags.map((t) => el('span', { class: 'tag', title: `Show sessions tagged ${t}`,
			onclick: (e) => { stop(e); this.search.value = `#${t}`; this.render(); } }, t,
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
			tagRow.append(el('button', { class: 'add-tag', text: '+ tag', onclick: (e) => { stop(e); startTagging(); } }));
		}

		// ⋯ menu
		const open = this.menu === s.key;
		const more = el('button', { class: 'more', text: '⋯', title: 'Actions', 'aria-haspopup': 'true', 'aria-expanded': open ? 'true' : 'false',
			onclick: (e) => { stop(e); this.menu = open ? null : s.key; this.render(); } });
		const item = (text, action, attrs = {}) => el('button', { text, ...attrs, onclick: (e) => { stop(e); this.menu = null; this.render(); action(); } });
		const menu = open ? el('div', { class: 'menu-list', onclick: stop },
			s.current ? null : item('Open', () => this.openSession(s), { disabled: this.running }),
			item('Rename', startRename),
			item('Add tag', startTagging),
			el('hr'),
			item('Download measurements (CSV)', () => download(`api/sessions/${s.key}/measurements.csv`)),
			s.audio ? item('Download audio', () => download(`api/sessions/${s.key}/audio`)) : null,
			el('hr'),
			item('Delete…', () => this.deleteSessions([s]), { class: 'danger', disabled: s.running })) : null;

		return el('div', {
			class: `session${s.current ? ' current' : ''}${this.selected.has(s.key) ? ' selected' : ''}`,
			title: s.current ? 'Shown' : this.running ? 'Stop the measurement to open another session' : 'Open this session',
			onclick: () => this.openSession(s),
		}, checkbox, title, more, el('div', { class: 'details', text: details }), tagRow, menu);
	}

	openSession(s) {
		if (s.current || this.running) return;
		this.run(async () => this.onOpened(await api.openSession(s.key)));
	}
}
