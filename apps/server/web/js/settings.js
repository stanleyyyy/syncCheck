// Settings drawer.
import { api } from './api.js';
import { el } from './format.js';

const SAMPLE_RATES = [44100, 48000, 88200, 96000, 192000];
const BLOCK_SIZES = [4096, 8192, 16384, 32768, 65536];

function field(label, control, help) {
	return el('div', { class: 'field' }, el('label', { text: label }), control, help ? el('div', { class: 'help', text: help }) : null);
}

function number(value, attrs = {}) {
	return el('input', { type: 'number', value: String(value), ...attrs });
}

function select(options, value) {
	const node = el('select', {}, options.map(([v, label]) => el('option', { value: String(v), text: label })));
	node.value = String(value);
	return node;
}

function segmented(options, value, onChange) {
	const node = el('div', { class: 'seg' });
	const render = (current) => {
		node.replaceChildren(...options.map(([v, label]) => el('button', {
			type: 'button', class: v === current ? 'on' : '', text: label,
			onclick: () => { render(v); onChange(v); },
		})));
	};
	render(value);
	return node;
}

export class SettingsDrawer {
	constructor({ drawer, scrim, body, onSaved }) {
		this.drawer = drawer;
		this.scrim = scrim;
		this.body = body;
		this.onSaved = onSaved;
		this.devices = { capture: [], playback: [] };
		this.settings = null;
		this.running = false;
		this.fileInfo = null;
	}

	async open(settings, running) {
		this.settings = structuredClone(settings);
		this.running = running;
		this.error = '';
		this.drawer.classList.remove('hidden');
		this.scrim.classList.remove('hidden');
		this.render();
		await this.loadDevices();
		if (this.settings.source === 'file' && this.settings.file) this.loadFileInfo(this.settings.file);
	}

	close() {
		this.drawer.classList.add('hidden');
		this.scrim.classList.add('hidden');
	}

	async loadDevices() {
		try {
			this.devices = await api.devices();
		} catch (e) {
			this.devices = { capture: [], playback: [] };
		}
		this.render();
	}

	async loadFileInfo(path) {
		try {
			this.fileInfo = await api.fileInfo(path);
			this.settings.channels = this.fileInfo.channels;
		} catch (e) {
			this.fileInfo = { error: e.message };
		}
		this.render();
	}

	// Channel count of the input: from the file, the device or the setting.
	inputChannels() {
		const s = this.settings;
		if (s.source === 'file' && this.fileInfo && this.fileInfo.channels) return this.fileInfo.channels;
		return s.channels;
	}

	async save() {
		try {
			const saved = await api.saveSettings(this.settings);
			this.close();
			this.onSaved(saved);
		} catch (e) {
			this.error = e.message;
			this.render();
		}
	}

	async reset() {
		try {
			this.settings = await api.resetSettings();
			this.fileInfo = null;
			this.error = '';
			this.onSaved(this.settings);
		} catch (e) {
			this.error = e.message;
		}
		this.render();
	}

	render() {
		const s = this.settings;
		if (!s) return;
		const rerender = () => this.render();
		const set = (key, convert = (v) => v) => (e) => { s[key] = convert(e.target.type === 'checkbox' ? e.target.checked : e.target.value); };
		const int = (v) => parseInt(v, 10) || 0;
		const children = [];

		if (this.running) {
			children.push(el('div', { class: 'notice', text: 'A measurement is running. Changes apply to the next measurement.' }));
		}

		// Input
		const input = el('fieldset', {}, el('legend', { text: 'Input' }));
		input.append(field('Source', segmented([['device', 'Audio device'], ['file', 'Audio file']], s.source, (v) => { s.source = v; rerender(); })));
		if (s.source === 'device') {
			const known = this.devices.capture.some((d) => d.name === s.device) || s.device === 'default';
			const options = [['default', 'System default'], ...this.devices.capture.map((d) =>
				[d.name, d.channels ? `${d.name} (${d.channels} ch)` : d.name]), ['__custom', 'Other…']];
			const deviceSelect = select(options, known ? s.device : '__custom');
			deviceSelect.addEventListener('change', (e) => {
				if (e.target.value === '__custom') {
					s.device = '';
				} else {
					s.device = e.target.value;
					// Use all channels the device has.
					const info = this.devices.capture.find((d) => d.name === s.device);
					if (info && info.channels >= 2) {
						s.channels = info.channels;
						s.measured = [];
					}
				}
				rerender();
			});
			input.append(field('Device', el('div', { class: 'row-inline' }, deviceSelect,
				el('button', { type: 'button', class: 'btn small', text: 'Refresh', onclick: () => this.loadDevices() }))));
			if (!known || deviceSelect.value === '__custom') {
				input.append(field('Device name', el('input', { type: 'text', value: s.device, placeholder: 'e.g. alsa:hw:1,0', oninput: set('device') }),
					'Device name or part of it, BACKEND:NAME, or alsa:PCM for raw ALSA devices'));
			}
			input.append(field('Channels', number(s.channels, { min: 1, max: 64, onchange: (e) => { s.channels = int(e.target.value); s.measured = []; rerender(); } })));
			input.append(field('Sample rate', (() => { const n = select(SAMPLE_RATES.map((r) => [r, `${r / 1000} kHz`]), s.sampleRate); n.onchange = set('sampleRate', int); return n; })()));
			input.append(field('Record input', el('label', { class: 'toggle' },
				el('input', { type: 'checkbox', checked: s.record, onchange: set('record') }), 'Keep the audio of the session'),
				'Needed to inspect correlation and waveforms of past moments'));
		} else {
			const fileInput = el('input', { type: 'file', accept: '.wav,.flac,.mp3,audio/*', class: 'hidden',
				onchange: async (e) => {
					const file = e.target.files[0];
					if (!file) return;
					this.fileInfo = { uploading: true };
					rerender();
					try {
						this.fileInfo = await api.upload(file);
						s.file = this.fileInfo.path;
						s.channels = this.fileInfo.channels;
						s.measured = [];
					} catch (err) {
						this.fileInfo = { error: err.message };
					}
					rerender();
				} });
			input.append(field('File', el('div', { class: 'row-inline' },
				el('input', { type: 'text', value: s.file, placeholder: 'Path on the server', onchange: (e) => { s.file = e.target.value; this.loadFileInfo(s.file); } }),
				el('button', { type: 'button', class: 'btn small', text: 'Upload…', onclick: () => fileInput.click() }), fileInput)));
			const info = this.fileInfo;
			const infoText = !info ? '' : info.uploading ? 'Uploading…' : info.error ? info.error
				: `${info.channels} ch · ${info.sampleRate / 1000} kHz · ${info.duration.toFixed(1)} s`;
			if (infoText) input.append(el('div', { class: 'field' }, el('span'), el('span', { class: info && info.error ? 'form-error' : 'muted', text: infoText })));
			input.append(field('Playback', el('label', { class: 'toggle' },
				el('input', { type: 'checkbox', checked: s.realtime, onchange: set('realtime') }), 'In real time'),
				'Off analyzes the file as fast as possible'));
		}
		input.append(field('Monitor', el('label', { class: 'toggle' },
			el('input', { type: 'checkbox', checked: s.monitor, onchange: (e) => { s.monitor = e.target.checked; rerender(); } }), 'Play the input on an output device')));
		if (s.monitor) {
			input.append(field('Output', select([['default', 'System default'], ...this.devices.playback.map((d) => [d.name, d.name])], s.monitorDevice)));
			input.lastChild.querySelector('select').onchange = set('monitorDevice');
		}
		children.push(input);

		// Measurement
		const measurement = el('fieldset', {}, el('legend', { text: 'Measurement' }));
		measurement.append(field('Compare', segmented([['channels', 'Channels'], ['sources', 'Bands']], s.mode, (v) => {
			s.mode = v;
			if (v === 'sources' && !s.sources) s.sources = 'mix';
			rerender();
		}), s.mode === 'channels'
			? 'Each channel against the reference channel. Works with music or noise on every channel.'
			: 'Frequency shifted bands of the band test signal, e.g. several devices recorded on 1–2 channels.'));

		const channels = this.inputChannels();
		if (s.mode === 'channels') {
			const references = Array.from({ length: channels }, (_, i) => [i, `Ch ${i + 1}`]);
			const ref = select(references, Math.min(s.reference, channels - 1));
			ref.onchange = (e) => { s.reference = int(e.target.value); s.measured = s.measured.filter((c) => c !== s.reference); rerender(); };
			measurement.append(field('Reference', ref));
			const chips = el('div', { class: 'chips' });
			for (let ch = 0; ch < channels; ch++) {
				if (ch === s.reference) continue;
				const checked = !s.measured.length || s.measured.includes(ch);
				chips.append(el('label', { class: 'chip' }, el('input', { type: 'checkbox', checked,
					onchange: (e) => {
						const all = Array.from({ length: channels }, (_, i) => i).filter((c) => c !== s.reference);
						let measured = s.measured.length ? [...s.measured] : all;
						measured = e.target.checked ? [...new Set([...measured, ch])] : measured.filter((c) => c !== ch);
						s.measured = measured.length === all.length ? [] : measured.sort((a, b) => a - b);
					} }), `Ch ${ch + 1}`));
			}
			measurement.append(field('Compare with', channels < 2 ? el('span', { class: 'form-error', text: 'Needs at least 2 input channels' }) : chips,
				'Channels whose delay against the reference is measured'));
			measurement.append(field('Frequency range', el('div', { class: 'row-inline' },
				number(s.minFreq, { min: 0, step: 100, onchange: set('minFreq', int) }), el('span', { class: 'muted', text: '–' }),
				number(s.maxFreq, { min: 100, step: 100, onchange: set('maxFreq', int) }), el('span', { class: 'muted', text: 'Hz' }))));
		} else {
			const mix = s.sources === 'mix';
			measurement.append(field('Input', el('label', { class: 'toggle' }, el('input', { type: 'checkbox', checked: mix,
				onchange: (e) => { s.sources = e.target.checked ? 'mix' : '0:0,1:0'; rerender(); } }), 'Mix all channels')));
			if (!mix) {
				measurement.append(field('Mapping', el('input', { type: 'text', value: s.sources, oninput: set('sources') }),
					'channel:band list, the first entry is the reference, e.g. 0:0,1:1'));
			}
			measurement.append(field('Bands', number(s.bands, { min: 2, max: 8, onchange: set('bands', int) }), 'At least 2: band 0 is the reference'));
			measurement.append(field('Start frequency', number(s.startFreq, { min: 0, step: 100, onchange: set('startFreq', int) })));
			measurement.append(field('Frequency range', number(s.freqRange, { min: 100, step: 100, onchange: set('freqRange', int) })));
			measurement.append(field('Band separation', number(s.bandSep, { min: 0, step: 50, onchange: set('bandSep', int) })));
		}
		children.push(measurement);

		// Advanced analysis settings
		const rate = s.source === 'file' && this.fileInfo && this.fileInfo.sampleRate ? this.fileInfo.sampleRate : s.sampleRate;
		const advanced = el('details', { class: 'advanced' }, el('summary', { text: 'Advanced' }));
		advanced.append(field('Block size', select(BLOCK_SIZES.map((b) => [b, `${b} (±${Math.round(b / 2 / rate * 1000)} ms)`]), s.blockSize),
			'Larger blocks measure larger delays and are more robust, but react slower'));
		advanced.lastChild.querySelector('select').onchange = set('blockSize', int);
		advanced.append(field('Overlap', select([[50, '50 %'], [75, '75 %']], s.overlap)));
		advanced.lastChild.querySelector('select').onchange = set('overlap', int);
		advanced.append(field('Interpolation', select([[1, '1×'], [2, '2×'], [4, '4×'], [8, '8×']], s.interp)));
		advanced.lastChild.querySelector('select').onchange = set('interp', int);
		advanced.append(field('Update every', el('div', { class: 'row-inline' }, number(s.refresh, { min: 0.05, step: 0.05, onchange: set('refresh', parseFloat) }),
			el('span', { class: 'muted', text: 's' }))));
		if (s.source === 'device') {
			advanced.append(field('Period size', number(s.periodSize, { min: 64, step: 64, onchange: set('periodSize', int) })));
			advanced.append(field('Periods', number(s.periods, { min: 2, max: 32, onchange: set('periods', int) })));
		}
		children.push(advanced);

		if (this.error) children.push(el('div', { class: 'form-error', text: this.error }));
		this.body.replaceChildren(...children);
	}
}
