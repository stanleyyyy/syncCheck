// Test signal dialog.
import { el } from './format.js';

export class SignalDialog {
	constructor({ dialog, body, download }) {
		this.dialog = dialog;
		this.body = body;
		this.download = download;
		this.options = { mode: 'channels', channels: 2, duration: 60, sampleRate: 48000, bands: 4, delays: [] };
	}

	open(settings) {
		const o = this.options;
		o.mode = settings.mode === 'sources' ? 'bands' : 'channels';
		o.channels = Math.max(2, settings.channels);
		o.sampleRate = settings.sampleRate;
		o.bands = Math.max(2, settings.bands);
		this.render();
		this.dialog.showModal();
	}

	count() {
		return this.options.mode === 'bands' ? this.options.bands : this.options.channels;
	}

	updateLink() {
		const o = this.options;
		const params = new URLSearchParams({ mode: o.mode, duration: o.duration, sampleRate: o.sampleRate,
			delays: Array.from({ length: this.count() }, (_, i) => o.delays[i] || 0).join(',') });
		if (o.mode === 'bands') params.set('bands', o.bands);
		else params.set('channels', o.channels);
		this.download.href = `api/generate?${params}`;
	}

	render() {
		const o = this.options;
		const rerender = () => this.render();
		const row = (label, control, help) => el('div', { class: 'field' }, el('label', { text: label }), control,
			help ? el('div', { class: 'help', text: help }) : null);
		const num = (key, attrs) => el('input', { type: 'number', value: String(o[key]), ...attrs,
			onchange: (e) => { o[key] = parseFloat(e.target.value) || 0; rerender(); } });

		const mode = el('select', {}, el('option', { value: 'channels', text: 'Noise on every channel' }),
			el('option', { value: 'bands', text: 'Band test signal (mono)' }));
		mode.value = o.mode;
		mode.onchange = (e) => { o.mode = e.target.value; rerender(); };

		const delays = el('div', { class: 'delays' });
		for (let i = 0; i < this.count(); i++) {
			const name = o.mode === 'bands' ? `Band ${i}` : `Ch ${i + 1}`;
			delays.append(el('label', {}, `${name} (ms)`, el('input', { type: 'number', step: '0.1', value: String(o.delays[i] || 0),
				onchange: (e) => { o.delays[i] = parseFloat(e.target.value) || 0; this.updateLink(); } })));
		}

		this.body.replaceChildren(
			row('Signal', mode, o.mode === 'bands'
				? 'For band mode: uses the band settings; play it through the devices you compare.'
				: 'For channel mode: identical white noise on all channels.'),
			o.mode === 'bands' ? row('Bands', num('bands', { min: 2, max: 8 })) : row('Channels', num('channels', { min: 2, max: 64 })),
			row('Duration', num('duration', { min: 1, max: 3600 }), 'Seconds'),
			row('Sample rate', num('sampleRate', { min: 8000, step: 100 })),
			el('fieldset', {}, el('legend', { text: 'Test delays' }), delays,
				el('p', { class: 'hint', text: 'Non-zero delays let you check the measurement against known values.' })),
		);
		this.updateLink();
	}
}
