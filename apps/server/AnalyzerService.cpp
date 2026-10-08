#include "AnalyzerService.h"
#include "Logger.h"
#include "WavFile.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

using nlohmann::json;
namespace fs = std::filesystem;

namespace
{

// Maximum number of points returned per correlation or waveform trace.
constexpr std::size_t MAX_TRACE_POINTS = 1500;

double round4(double value)
{
	return std::isfinite(value) ? std::round(value * 10000.0) / 10000.0 : 0.0;
}

std::string timestamp(const char *format)
{
	std::time_t now = std::time(nullptr);
	std::tm tm {};
#ifdef _WIN32
	localtime_s(&tm, &now);
#else
	localtime_r(&now, &tm);
#endif
	char buffer[64];
	std::strftime(buffer, sizeof(buffer), format, &tm);
	return buffer;
}

json assignmentJson(const FFTSyncCheck::ChannelAssignment &assignment)
{
	return { { "ref", assignment.refChannel }, { "refBand", assignment.refBand },
		{ "sig", assignment.sigChannel }, { "sigBand", assignment.sigBand } };
}

} // namespace

ServerSettings::ServerSettings()
{
	// The web UI prefers comparing native channels.
	m_analyzer.m_mode = SyncAnalyzerMode::Channels;
	m_analyzer.m_blockSize = 16384;
	m_analyzer.m_statsRefreshRate = 0.25f;
	m_analyzer.m_realtimeFile = true;
}

void to_json(json &j, const ServerSettings &settings)
{
	const SyncAnalyzerSettings &a = settings.m_analyzer;
	j = json {
		{ "source", settings.m_source },
		{ "file", settings.m_file },
		{ "record", settings.m_record },
		{ "monitor", settings.m_monitor },
		{ "monitorDevice", settings.m_monitorDevice },
		{ "device", a.m_captureDevice },
		{ "channels", a.m_channels },
		{ "sampleRate", a.m_sampleRate },
		{ "periodSize", a.m_periodSize },
		{ "periods", a.m_periods },
		{ "realtime", a.m_realtimeFile },
		{ "mode", a.m_mode == SyncAnalyzerMode::Channels ? "channels" : "sources" },
		{ "reference", a.m_referenceChannel },
		{ "measured", a.m_measuredChannels },
		{ "minFreq", a.m_minFreq },
		{ "maxFreq", a.m_maxFreq },
		{ "sources", a.m_sources },
		{ "bands", a.m_numBands },
		{ "startFreq", a.m_startFreq },
		{ "freqRange", a.m_freqRange },
		{ "bandSep", a.m_bandSep },
		{ "blockSize", a.m_blockSize },
		{ "overlap", a.m_overlapPercent },
		{ "interp", a.m_interpFactor },
		{ "refresh", a.m_statsRefreshRate },
	};
}

void from_json(const json &j, ServerSettings &settings)
{
	// Only the given keys are changed, so partial updates are possible.
	SyncAnalyzerSettings &a = settings.m_analyzer;
	auto get = [&](const char *key, auto &value) {
		if (j.contains(key) && !j.at(key).is_null()) {
			j.at(key).get_to(value);
		}
	};
	get("source", settings.m_source);
	get("file", settings.m_file);
	get("record", settings.m_record);
	get("monitor", settings.m_monitor);
	get("monitorDevice", settings.m_monitorDevice);
	get("device", a.m_captureDevice);
	get("channels", a.m_channels);
	get("sampleRate", a.m_sampleRate);
	get("periodSize", a.m_periodSize);
	get("periods", a.m_periods);
	get("realtime", a.m_realtimeFile);
	if (j.contains("mode")) {
		a.m_mode = j.at("mode").get<std::string>() == "sources" ? SyncAnalyzerMode::Sources : SyncAnalyzerMode::Channels;
	}
	get("reference", a.m_referenceChannel);
	get("measured", a.m_measuredChannels);
	get("minFreq", a.m_minFreq);
	get("maxFreq", a.m_maxFreq);
	get("sources", a.m_sources);
	get("bands", a.m_numBands);
	get("startFreq", a.m_startFreq);
	get("freqRange", a.m_freqRange);
	get("bandSep", a.m_bandSep);
	get("blockSize", a.m_blockSize);
	get("overlap", a.m_overlapPercent);
	get("interp", a.m_interpFactor);
	get("refresh", a.m_statsRefreshRate);
}

AnalyzerService::AnalyzerService(const std::string &dataDir)
	: m_dataDir(dataDir)
{
	std::error_code ec;
	fs::create_directories(fs::path(m_dataDir) / "sessions", ec);
	loadSettings();
}

AnalyzerService::~AnalyzerService()
{
	shutdown();
}

void AnalyzerService::loadSettings()
{
	std::ifstream file(fs::path(m_dataDir) / "settings.json");
	if (!file) {
		return;
	}
	try {
		from_json(json::parse(file), m_settings);
	} catch (const std::exception &e) {
		DBG_WARN("Ignoring invalid settings.json: " << e.what());
	}
}

void AnalyzerService::saveSettings() const
{
	std::ofstream file(fs::path(m_dataDir) / "settings.json");
	file << json(m_settings).dump(2);
}

ServerSettings AnalyzerService::settings() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_settings;
}

void AnalyzerService::setSettings(const ServerSettings &settings)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_settings = settings;
	saveSettings();
	m_stateVersion++;
	notify();
}

void AnalyzerService::notify()
{
	m_version++;
	m_changed.notify_all();
}

void AnalyzerService::setStatus(const std::string &status, bool running)
{
	m_status = status;
	m_running = running;
	m_stateVersion++;
	notify();
}

bool AnalyzerService::start(std::string &error)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_running) {
			error = "a measurement is already running";
			return false;
		}
	}
	if (m_worker.joinable()) {
		m_worker.join();
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	SyncAnalyzerSettings settings = m_settings.m_analyzer;
	if (!SyncAnalyzer::validate(settings, error)) {
		return false;
	}

	Session session;
	session.m_id = ++m_lastSessionId;
	session.m_started = timestamp("%Y-%m-%d %H:%M:%S");
	session.m_dir = (fs::path(m_dataDir) / "sessions" / timestamp("%Y%m%d-%H%M%S")).string();
	std::error_code ec;
	fs::create_directories(session.m_dir, ec);

	if (m_settings.m_source == "file") {
		if (m_settings.m_file.empty()) {
			error = "no input file selected";
			return false;
		}
		settings.m_captureDevice = "file://" + m_settings.m_file;
		session.m_audio = m_settings.m_file;
		session.m_input = fs::path(m_settings.m_file).filename().string();
	} else {
		session.m_input = settings.m_captureDevice;
		if (m_settings.m_record) {
			settings.m_recordFile = (fs::path(session.m_dir) / "recording.wav").string();
			session.m_audio = settings.m_recordFile;
		}
	}
	settings.m_playbackDevice = m_settings.m_monitor ? m_settings.m_monitorDevice : "";
	session.m_settings = settings;

	std::ofstream(fs::path(session.m_dir) / "settings.json") << json(m_settings).dump(2);

	m_session = std::move(session);
	m_stopRequested = false;
	setStatus("Starting", true);

	m_worker = std::thread([this, settings] {
		std::string runError;
		const bool ok = m_analyzer.run(settings, [this](const SyncAnalyzerSnapshot &snapshot) { onSnapshot(snapshot); }, runError);

		std::lock_guard<std::mutex> lock(m_mutex);
		if (!ok) {
			DBG_ERROR("Measurement failed: " << runError);
		}
		std::ofstream(fs::path(m_session.m_dir) / "measurements.csv") << measurementsCsvLocked();
		setStatus(!ok ? "Error: " + runError : m_stopRequested ? "Stopped" : "Finished", false);
	});
	return true;
}

void AnalyzerService::stop()
{
	m_stopRequested = true;
	m_analyzer.stop();
	if (m_worker.joinable()) {
		m_worker.join();
	}
}

void AnalyzerService::shutdown()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_shutdown = true;
		notify();
	}
	stop();
}

bool AnalyzerService::isShutDown() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_shutdown;
}

void AnalyzerService::onSnapshot(const SyncAnalyzerSnapshot &snapshot)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	Session &session = m_session;

	// Nothing has been measured before the first full block.
	if (snapshot.m_frames < session.m_settings.m_blockSize) {
		return;
	}

	if (session.m_pairs.empty()) {
		session.m_sampleRate = snapshot.m_sampleRate;
		session.m_channels = snapshot.m_channels;
		session.m_interpFactor = snapshot.m_interpFactor;
		for (const auto &result : snapshot.m_results) {
			Pair pair;
			pair.m_assignment = result.m_report.assignment;
			session.m_pairs.push_back(pair);
		}
		m_status = "Running";
		m_stateVersion++;
	}

	session.m_time.push_back(double(snapshot.m_frames) / snapshot.m_sampleRate);
	for (std::size_t i = 0; i < session.m_pairs.size() && i < snapshot.m_results.size(); i++) {
		const auto &report = snapshot.m_results[i].m_report;
		Pair &pair = session.m_pairs[i];
		pair.m_delay.push_back(report.filteredDelay);
		pair.m_confidence.push_back(report.confidence);
		pair.m_pslr.push_back(report.filteredPSLR);
		pair.m_refRms.push_back(report.filteredRefRMS);
		pair.m_sigRms.push_back(report.filteredSigRMS);
	}
	session.m_latest = snapshot;
	notify();
}

json AnalyzerService::pairsLocked() const
{
	json pairs = json::array();
	for (const auto &pair : m_session.m_pairs) {
		pairs.push_back(assignmentJson(pair.m_assignment));
	}
	return pairs;
}

json AnalyzerService::stateLocked() const
{
	const Session &session = m_session;
	json state = {
		{ "running", m_running },
		{ "status", m_status },
		{ "settings", m_settings },
		{ "dataDir", m_dataDir },
	};
	if (session.m_id) {
		state["session"] = {
			{ "id", session.m_id },
			{ "started", session.m_started },
			{ "input", session.m_input },
			{ "dir", session.m_dir },
			{ "audio", !session.m_audio.empty() },
			{ "mode", session.m_settings.m_mode == SyncAnalyzerMode::Channels ? "channels" : "sources" },
			{ "sampleRate", session.m_sampleRate },
			{ "channels", session.m_channels },
			{ "blockSize", session.m_settings.m_blockSize },
			{ "pairs", pairsLocked() },
			{ "duration", session.m_time.empty() ? 0.0 : session.m_time.back() },
		};
	}
	return state;
}

json AnalyzerService::state() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return stateLocked();
}

json AnalyzerService::measurementsLocked(std::size_t from) const
{
	const Session &session = m_session;
	from = std::min(from, session.m_time.size());

	json result = { { "session", session.m_id }, { "from", from } };
	json times = json::array();
	for (std::size_t i = from; i < session.m_time.size(); i++) {
		times.push_back(std::round(session.m_time[i] * 1000.0) / 1000.0);
	}
	result["t"] = std::move(times);

	json pairs = json::array();
	for (const auto &pair : session.m_pairs) {
		auto column = [&](const std::vector<float> &values) {
			json out = json::array();
			for (std::size_t i = from; i < values.size(); i++) {
				out.push_back(round4(values[i]));
			}
			return out;
		};
		pairs.push_back({ { "delay", column(pair.m_delay) }, { "confidence", column(pair.m_confidence) },
			{ "pslr", column(pair.m_pslr) }, { "refRms", column(pair.m_refRms) }, { "sigRms", column(pair.m_sigRms) } });
	}
	result["pairs"] = std::move(pairs);
	return result;
}

json AnalyzerService::measurements(std::size_t from) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return measurementsLocked(from);
}

std::string AnalyzerService::measurementsCsvLocked() const
{
	std::ostringstream csv;
	csv << "time_s,ref_channel,ref_band,sig_channel,sig_band,delay_ms,confidence,pslr_db,ref_rms_db,sig_rms_db\n";
	csv << std::fixed << std::setprecision(4);
	for (std::size_t i = 0; i < m_session.m_time.size(); i++) {
		for (const auto &pair : m_session.m_pairs) {
			if (i >= pair.m_delay.size()) {
				continue;
			}
			const auto &a = pair.m_assignment;
			csv << m_session.m_time[i] << "," << a.refChannel << "," << a.refBand << "," << a.sigChannel << "," << a.sigBand << ","
				<< pair.m_delay[i] << "," << pair.m_confidence[i] << "," << pair.m_pslr[i] << ","
				<< pair.m_refRms[i] << "," << pair.m_sigRms[i] << "\n";
		}
	}
	return csv.str();
}

std::string AnalyzerService::measurementsCsv() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return measurementsCsvLocked();
}

std::string AnalyzerService::sessionAudio() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_session.m_audio;
}

bool AnalyzerService::inspect(double time, double rangeMs, double windowMs, bool live, json &result, std::string &error) const
{
	std::string audio;
	SyncAnalyzerSettings settings;
	SyncAnalyzerSnapshot snapshot;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_session.m_id) {
			error = "no session";
			return false;
		}
		audio = m_session.m_audio;
		settings = m_session.m_settings;
		if (live || audio.empty()) {
			if (!m_session.m_latest.m_sequence) {
				error = "no measurement yet";
				return false;
			}
			snapshot = m_session.m_latest;
			live = true;
		}
	}

	WavReader reader;
	const bool haveAudio = !audio.empty() && reader.open(audio);
	const uint64_t available = haveAudio ? reader.getLengthInFrames() : 0;

	if (!live) {
		if (!haveAudio) {
			error = "cannot open the session audio";
			return false;
		}
		const unsigned int sampleRate = reader.getSampleRate();
		uint64_t endFrame = static_cast<uint64_t>(std::llround(std::max(0.0, time) * sampleRate));
		endFrame = std::clamp<uint64_t>(endFrame, settings.m_blockSize, std::max<uint64_t>(available, settings.m_blockSize));
		if (!SyncAnalyzer::analyzeBlock(audio, settings, endFrame, snapshot, error)) {
			return false;
		}
	}

	const unsigned int sampleRate = snapshot.m_sampleRate;
	const double time0 = double(snapshot.m_frames) / sampleRate;
	result = { { "time", time0 }, { "live", live }, { "sampleRate", sampleRate } };

	// Correlations on a common lag grid in ms, normalized to the peak; each grid point
	// keeps the maximum of its bucket so that narrow peaks stay visible.
	const double msPerSample = 1000.0 / (double(sampleRate) * snapshot.m_interpFactor);
	json lags = json::array();
	json pairs = json::array();
	for (const auto &r : snapshot.m_results) {
		json pair = assignmentJson(r.m_report.assignment);
		pair["delay"] = round4(r.m_report.filteredDelay);
		pair["pslr"] = round4(r.m_report.filteredPSLR);
		pair["confidence"] = round4(r.m_report.confidence);

		const std::vector<float> &corr = r.m_correlation;
		json ys = json::array();
		if (!corr.empty()) {
			const long n = static_cast<long>(corr.size());
			const long center = n / 2;
			const long span = std::lround(rangeMs / msPerSample);
			const long first = std::max(0L, center - span);
			const long last = std::min(n - 1, center + span);
			const float peak = std::max(1e-12f, *std::max_element(corr.begin(), corr.end()));
			const long step = std::max(1L, (last - first + long(MAX_TRACE_POINTS)) / long(MAX_TRACE_POINTS));
			const bool fillLags = lags.empty();
			for (long i = first; i <= last; i += step) {
				float best = corr[i];
				for (long k = i; k < std::min(i + step, last + 1); k++) {
					best = std::max(best, corr[k]);
				}
				if (fillLags) {
					lags.push_back(round4((i - center) * msPerSample));
				}
				ys.push_back(round4(best / peak));
			}
		}
		pair["corr"] = std::move(ys);
		pairs.push_back(std::move(pair));
	}
	result["lag"] = std::move(lags);
	result["pairs"] = std::move(pairs);

	// Waveforms of all channels around the moment.
	if (haveAudio && reader.getSampleRate() == sampleRate) {
		const unsigned int channels = reader.getChannels();
		const uint64_t center = std::min<uint64_t>(snapshot.m_frames, available);
		const uint64_t half = static_cast<uint64_t>(windowMs / 2000.0 * sampleRate);
		const uint64_t start = center > half ? center - half : 0;
		const uint64_t end = std::min<uint64_t>(center + half, available);
		if (end > start && reader.seek(start)) {
			const std::size_t frames = static_cast<std::size_t>(end - start);
			std::vector<float> samples(frames * channels, 0.0f);
			const std::size_t read = reader.readFrames(samples.data(), frames);
			const std::size_t buckets = std::min(read, MAX_TRACE_POINTS);
			const double framesPerBucket = buckets ? double(read) / buckets : 1.0;

			json traces = json::array();
			for (unsigned int ch = 0; ch < channels; ch++) {
				json mins = json::array(), maxs = json::array();
				for (std::size_t b = 0; b < buckets; b++) {
					const std::size_t i0 = std::size_t(b * framesPerBucket);
					const std::size_t i1 = std::max(i0 + 1, std::size_t((b + 1) * framesPerBucket));
					float lo = 1.0f, hi = -1.0f;
					for (std::size_t i = i0; i < i1 && i < read; i++) {
						lo = std::min(lo, samples[i * channels + ch]);
						hi = std::max(hi, samples[i * channels + ch]);
					}
					mins.push_back(round4(lo));
					maxs.push_back(round4(hi));
				}
				traces.push_back({ { "channel", ch }, { "min", std::move(mins) }, { "max", std::move(maxs) } });
			}
			result["waveform"] = {
				{ "startMs", (double(start) - double(center)) * 1000.0 / sampleRate },
				{ "stepMs", framesPerBucket * 1000.0 / sampleRate },
				{ "channels", std::move(traces) },
			};
		}
	}
	return true;
}

void AnalyzerService::waitForChange(const Cursor &cursor, std::chrono::milliseconds timeout) const
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_changed.wait_for(lock, timeout, [&] { return m_shutdown || m_version != cursor.m_version; });
}

std::string AnalyzerService::eventsSince(Cursor &cursor) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::string out;

	if (cursor.m_session != m_session.m_id) {
		cursor.m_session = m_session.m_id;
		cursor.m_row = 0;
		cursor.m_stateVersion = 0;
	}
	if (cursor.m_stateVersion != m_stateVersion) {
		cursor.m_stateVersion = m_stateVersion;
		out += "event: state\ndata: " + stateLocked().dump() + "\n\n";
	}
	if (m_session.m_time.size() > cursor.m_row) {
		const json rows = measurementsLocked(cursor.m_row);
		cursor.m_row = m_session.m_time.size();
		out += "id: " + std::to_string(m_session.m_id) + ":" + std::to_string(cursor.m_row) + "\n";
		out += "event: measurements\ndata: " + rows.dump() + "\n\n";
	}
	cursor.m_version = m_version;
	return out;
}
