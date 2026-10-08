// Server API.

async function request(method, path, body) {
	const options = { method, headers: {} };
	if (body !== undefined) {
		options.headers['Content-Type'] = 'application/json';
		options.body = JSON.stringify(body);
	}
	const response = await fetch(path, options);
	const data = await response.json().catch(() => ({}));
	if (!response.ok) {
		throw new Error(data.error || `${response.status} ${response.statusText}`);
	}
	return data;
}

export const api = {
	state: () => request('GET', 'api/state'),
	devices: () => request('GET', 'api/devices'),
	saveSettings: (settings) => request('PUT', 'api/settings', settings),
	resetSettings: () => request('POST', 'api/settings/reset'),
	sessions: () => request('GET', 'api/sessions'),
	openSession: (key) => request('POST', `api/sessions/${encodeURIComponent(key)}/open`),
	renameSession: (key, name) => request('PUT', `api/sessions/${encodeURIComponent(key)}`, { name }),
	tagSession: (key, tags) => request('PUT', `api/sessions/${encodeURIComponent(key)}`, { tags }),
	deleteSession: (key) => request('DELETE', `api/sessions/${encodeURIComponent(key)}`),
	start: () => request('POST', 'api/start'),
	stop: () => request('POST', 'api/stop'),
	fileInfo: (path) => request('GET', `api/fileinfo?path=${encodeURIComponent(path)}`),
	inspect: (params) => request('GET', `api/inspect?${new URLSearchParams(params)}`),
	waveform: (params) => request('GET', `api/waveform?${new URLSearchParams(params)}`),
	overview: (params) => request('GET', `api/overview?${new URLSearchParams(params)}`),
	async upload(file) {
		const form = new FormData();
		form.append('file', file);
		const response = await fetch('api/upload', { method: 'POST', body: form });
		const data = await response.json().catch(() => ({}));
		if (!response.ok) throw new Error(data.error || 'upload failed');
		return data;
	},
};

// Live updates: 'state' and 'measurements' events. EventSource reconnects by itself and
// resumes from the last received measurement (Last-Event-ID).
export function subscribe({ onState, onMeasurements, onDisk, onConnection }) {
	const source = new EventSource('api/events');
	source.addEventListener('disk', (e) => onDisk(JSON.parse(e.data)));
	source.addEventListener('state', (e) => onState(JSON.parse(e.data)));
	source.addEventListener('measurements', (e) => onMeasurements(JSON.parse(e.data)));
	source.onopen = () => onConnection(true);
	source.onerror = () => onConnection(false);
	return source;
}
