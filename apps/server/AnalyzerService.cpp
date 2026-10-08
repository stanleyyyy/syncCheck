#include "AnalyzerService.h"
#include "Logger.h"
#include "WavFile.h"
#include <algorithm>
#include <cctype>
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

// Min/max envelope of every channel over the frames first..last, in at most maxPoints buckets.
// Channel ch is read shift[ch] frames later; samples outside the file stay silent.
json readEnvelope(WavReader &reader, uint64_t available, long first, long last, const std::vector<long> &shift,
	std::size_t maxPoints, double &framesPerBucket)
{
	const unsigned int channels = reader.getChannels();
	const long minShift = std::min(0L, *std::min_element(shift.begin(), shift.end()));
	const long maxShift = std::max(0L, *std::max_element(shift.begin(), shift.end()));
	// Read the range covering all shifts.
	const long readFrom = first + minShift;
	const long readTo = last + maxShift;
	const long clampedFrom = std::clamp(readFrom, 0L, long(available));
	const long clampedTo = std::clamp(readTo, 0L, long(available));
	std::vector<float> samples(std::size_t(readTo - readFrom) * channels, 0.0f);
	if (clampedTo > clampedFrom && reader.seek(std::size_t(clampedFrom))) {
		reader.readFrames(samples.data() + std::size_t(clampedFrom - readFrom) * channels, std::size_t(clampedTo - clampedFrom));
	}
	const std::size_t frames = std::size_t(last - first);
	const std::size_t buckets = std::min(frames, maxPoints);
	framesPerBucket = double(frames) / buckets;

	json traces = json::array();
	for (unsigned int ch = 0; ch < channels; ch++) {
		const long base = first + shift[ch] - readFrom;
		json mins = json::array(), maxs = json::array();
		for (std::size_t b = 0; b < buckets; b++) {
			const std::size_t i0 = std::size_t(b * framesPerBucket);
			const std::size_t i1 = std::max(i0 + 1, std::size_t((b + 1) * framesPerBucket));
			float lo = 1.0f, hi = -1.0f;
			for (std::size_t i = i0; i < i1 && i < frames; i++) {
				const float sample = samples[(std::size_t(base) + i) * channels + ch];
				lo = std::min(lo, sample);
				hi = std::max(hi, sample);
			}
			// Full sample precision; quiet signals would turn into steps otherwise.
			mins.push_back(std::round(double(lo) * 1e7) / 1e7);
			maxs.push_back(std::round(double(hi) * 1e7) / 1e7);
		}
		traces.push_back({ { "channel", ch }, { "min", std::move(mins) }, { "max", std::move(maxs) } });
	}
	return traces;
}

} // namespace

ServerSettings::ServerSettings()
{
	// The web UI prefers comparing native channels.
	m_analyzer.m_mode = SyncAnalyzerMode::Channels;
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

void AnalyzerService::resetSettings()
{
	setSettings(ServerSettings());
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
	session.m_startedAt = std::chrono::steady_clock::now();
	session.m_source = m_settings.m_source;

	// The directory name identifies the session (and its exported files).
	const fs::path sessions = fs::path(m_dataDir) / "sessions";
	session.m_key = timestamp("%Y%m%d-%H%M%S");
	for (int n = 2; fs::exists(sessions / session.m_key); n++) {
		session.m_key = timestamp("%Y%m%d-%H%M%S") + "-" + std::to_string(n);
	}
	session.m_dir = (sessions / session.m_key).string();
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
			session.m_recorded = true;
		}
	}
	settings.m_playbackDevice = m_settings.m_monitor ? m_settings.m_monitorDevice : "";
	session.m_settings = settings;
	session.m_name = session.m_input;

	std::ofstream(fs::path(session.m_dir) / "settings.json") << json(m_settings).dump(2);

	// Measurements are written while measuring, so an interrupted session can be opened too.
	session.m_csv = std::make_unique<std::ofstream>(fs::path(session.m_dir) / "measurements.csv");
	*session.m_csv << "time_s,ref_channel,ref_band,sig_channel,sig_band,delay_ms,confidence,pslr_db,ref_rms_db,sig_rms_db\n";

	m_session = std::move(session);
	writeSessionInfoLocked();
	m_stopRequested = false;
	setStatus("Starting", true);

	m_worker = std::thread([this, settings] {
		std::string runError;
		const bool ok = m_analyzer.run(settings, [this](const SyncAnalyzerSnapshot &snapshot) { onSnapshot(snapshot); }, runError);

		std::lock_guard<std::mutex> lock(m_mutex);
		if (!ok) {
			DBG_ERROR("Measurement failed: " << runError);
		}
		m_session.m_csv.reset();
		setStatus(!ok ? "Error: " + runError : m_stopRequested ? "Stopped" : "Finished", false);
		writeSessionInfoLocked();
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
		writeSessionInfoLocked();
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
	appendCsvLocked();
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
			{ "key", session.m_key },
			{ "name", session.m_name },
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

std::string AnalyzerService::sessionAudio() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_session.m_audio;
}

bool AnalyzerService::inspect(double time, double rangeMs, double windowMs, double offsetMs, bool live, bool align, json &result,
	std::string &error) const
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
		// An opened session has no live measurement; use its last moment.
		if (live && !m_session.m_latest.m_sequence && !audio.empty() && !m_session.m_time.empty()) {
			live = false;
			time = m_session.m_time.back();
		}
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

	// Waveforms of all channels: the requested window and, for navigation, an overview of the
	// analyzed block (which ends at the moment). Times are in ms relative to the moment.
	if (haveAudio && reader.getSampleRate() == sampleRate) {
		const unsigned int channels = reader.getChannels();
		const uint64_t moment = std::min<uint64_t>(snapshot.m_frames, available);

		// Align: a compared channel is read later by its measured delay, so its audio lines up with the reference.
		std::vector<long> shift(channels, 0);
		if (align) {
			for (const auto &r : snapshot.m_results) {
				const std::size_t ch = r.m_report.assignment.sigChannel;
				if (ch < channels && ch != r.m_report.assignment.refChannel) {
					shift[ch] = std::lround(r.m_report.filteredDelay / 1000.0 * sampleRate);
				}
			}
		}
		const std::vector<long> noShift(channels, 0);

		auto envelope = [&](double fromMs, double toMs, std::size_t maxPoints, bool shifted) -> json {
			const long first = std::lround(double(moment) + fromMs / 1000.0 * sampleRate);
			const long last = std::lround(double(moment) + toMs / 1000.0 * sampleRate);
			if (last <= first) {
				return nullptr;
			}
			double framesPerBucket = 1.0;
			json traces = readEnvelope(reader, available, first, last, shifted ? shift : noShift, maxPoints, framesPerBucket);
			return {
				{ "startMs", (double(first) - double(moment)) * 1000.0 / sampleRate },
				{ "stepMs", framesPerBucket * 1000.0 / sampleRate },
				{ "channels", std::move(traces) },
			};
		};

		const json wave = envelope(offsetMs - windowMs / 2, offsetMs + windowMs / 2, MAX_TRACE_POINTS, true);
		if (!wave.is_null()) {
			result["waveform"] = wave;
		}
		const double blockMs = 1000.0 * settings.m_blockSize / sampleRate;
		const json overview = envelope(-blockMs, 0.0, 600, false);
		if (!overview.is_null()) {
			result["overview"] = overview;
			result["blockMs"] = blockMs;
		}
	}
	return true;
}

bool AnalyzerService::waveform(double from, double to, const std::map<unsigned int, double> &delaysMs, json &result,
	std::string &error) const
{
	std::string audio;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		audio = m_session.m_audio;
	}
	WavReader reader;
	if (audio.empty() || !reader.open(audio)) {
		error = "no session audio";
		return false;
	}
	const unsigned int sampleRate = reader.getSampleRate();
	const unsigned int channels = reader.getChannels();
	const uint64_t available = reader.getLengthInFrames();
	result = { { "duration", double(available) / sampleRate }, { "sampleRate", sampleRate } };

	// Align: a compared channel is read later by its delay, so its audio lines up with the reference.
	std::vector<long> shift(channels, 0);
	for (const auto &[channel, delay] : delaysMs) {
		if (channel < channels) {
			shift[channel] = std::lround(delay / 1000.0 * sampleRate);
		}
	}
	const long first = std::lround(from * sampleRate);
	const long last = std::lround(to * sampleRate);
	if (last > first) {
		double framesPerBucket = 1.0;
		result["channels"] = readEnvelope(reader, available, first, last, shift, MAX_TRACE_POINTS, framesPerBucket);
		result["start"] = double(first) / sampleRate;
		result["step"] = framesPerBucket / sampleRate;
	}
	return true;
}

bool AnalyzerService::overview(std::size_t points, json &result, std::string &error) const
{
	constexpr uint64_t BUCKET_FRAMES = 1024;
	std::string audio;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		audio = m_session.m_audio;
	}
	WavReader reader;
	if (audio.empty() || !reader.open(audio)) {
		error = "no session audio";
		return false;
	}
	const unsigned int channels = reader.getChannels();
	const uint64_t available = reader.getLengthInFrames();

	std::lock_guard<std::mutex> lock(m_overviewMutex);
	OverviewCache &cache = m_overview;
	if (cache.m_audio != audio || cache.m_channels != channels || cache.m_frames > available) {
		cache = OverviewCache();
		cache.m_audio = audio;
		cache.m_channels = channels;
		cache.m_sampleRate = reader.getSampleRate();
	}
	// Add the buckets of the audio written since the last call.
	const uint64_t frames = available / BUCKET_FRAMES * BUCKET_FRAMES;
	if (frames > cache.m_frames && reader.seek(std::size_t(cache.m_frames))) {
		std::vector<float> samples(BUCKET_FRAMES * channels);
		for (uint64_t at = cache.m_frames; at < frames; at += BUCKET_FRAMES) {
			if (reader.readFrames(samples.data(), BUCKET_FRAMES) < BUCKET_FRAMES) {
				break;
			}
			for (unsigned int ch = 0; ch < channels; ch++) {
				float lo = 1.0f, hi = -1.0f;
				for (uint64_t i = 0; i < BUCKET_FRAMES; i++) {
					lo = std::min(lo, samples[i * channels + ch]);
					hi = std::max(hi, samples[i * channels + ch]);
				}
				cache.m_min.push_back(lo);
				cache.m_max.push_back(hi);
			}
			cache.m_frames = at + BUCKET_FRAMES;
		}
	}

	// Merge the cached buckets into at most points buckets.
	const std::size_t cached = std::size_t(cache.m_frames / BUCKET_FRAMES);
	const std::size_t buckets = std::min(cached, std::max<std::size_t>(points, 1));
	const double perBucket = buckets ? double(cached) / buckets : 1.0;
	json traces = json::array();
	for (unsigned int ch = 0; ch < channels; ch++) {
		json mins = json::array(), maxs = json::array();
		for (std::size_t b = 0; b < buckets; b++) {
			const std::size_t i0 = std::size_t(b * perBucket);
			const std::size_t i1 = std::max(i0 + 1, std::size_t((b + 1) * perBucket));
			float lo = 1.0f, hi = -1.0f;
			for (std::size_t i = i0; i < i1 && i < cached; i++) {
				lo = std::min(lo, cache.m_min[i * channels + ch]);
				hi = std::max(hi, cache.m_max[i * channels + ch]);
			}
			mins.push_back(std::round(double(lo) * 1e5) / 1e5);
			maxs.push_back(std::round(double(hi) * 1e5) / 1e5);
		}
		traces.push_back({ { "channel", ch }, { "min", std::move(mins) }, { "max", std::move(maxs) } });
	}
	const unsigned int sampleRate = cache.m_sampleRate;
	result = { { "duration", double(available) / sampleRate }, { "sampleRate", sampleRate }, { "start", 0.0 },
		{ "step", perBucket * BUCKET_FRAMES / sampleRate }, { "channels", std::move(traces) } };
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
	// Disk usage about once per second while recording.
	const auto now = std::chrono::steady_clock::now();
	if (m_running && m_session.m_recorded && now - cursor.m_lastDisk >= std::chrono::seconds(1)) {
		cursor.m_lastDisk = now;
		out += "event: disk\ndata: " + diskLocked().dump() + "\n\n";
	}
	cursor.m_version = m_version;
	return out;
}

void AnalyzerService::appendCsvLocked()
{
	Session &session = m_session;
	if (!session.m_csv || session.m_time.empty()) {
		return;
	}
	const std::size_t i = session.m_time.size() - 1;
	std::ofstream &csv = *session.m_csv;
	csv << std::fixed << std::setprecision(4);
	for (const auto &pair : session.m_pairs) {
		const auto &a = pair.m_assignment;
		csv << session.m_time[i] << "," << a.refChannel << "," << a.refBand << "," << a.sigChannel << "," << a.sigBand << ","
			<< pair.m_delay[i] << "," << pair.m_confidence[i] << "," << pair.m_pslr[i] << "," << pair.m_refRms[i] << "," << pair.m_sigRms[i] << "\n";
	}
	if (i % 20 == 0) {
		csv.flush();
	}
}

void AnalyzerService::writeSessionInfoLocked() const
{
	const Session &session = m_session;
	if (session.m_dir.empty()) {
		return;
	}
	ServerSettings settings = m_settings;
	settings.m_analyzer = session.m_settings;
	const json info = {
		{ "key", session.m_key },
		{ "name", session.m_name },
		{ "tags", session.m_tags },
		{ "started", session.m_started },
		{ "input", session.m_input },
		{ "source", session.m_source },
		{ "audio", session.m_audio },
		{ "recorded", session.m_recorded },
		{ "sampleRate", session.m_sampleRate },
		{ "channels", session.m_channels },
		{ "interp", session.m_interpFactor },
		{ "pairs", pairsLocked() },
		{ "duration", session.m_time.empty() ? 0.0 : session.m_time.back() },
		{ "status", m_status },
		{ "settings", settings },
	};
	std::ofstream(fs::path(session.m_dir) / "session.json") << info.dump(2);
}

json AnalyzerService::diskLocked() const
{
	json result = json::object();
	std::error_code ec;
	const fs::space_info space = fs::space(m_dataDir, ec);
	if (!ec) {
		result["free"] = space.available;
		result["capacity"] = space.capacity;
	}
	if (m_session.m_recorded && !m_session.m_audio.empty()) {
		const uintmax_t size = fs::file_size(m_session.m_audio, ec);
		if (!ec) {
			result["file"] = fs::path(m_session.m_audio).filename().string();
			result["size"] = size;
			const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_session.m_startedAt).count();
			if (m_running && seconds > 1.0) {
				const double rate = double(size) / seconds;
				result["rate"] = rate;
				if (result.contains("free") && rate > 0) {
					result["remaining"] = double(space.available) / rate;
				}
			}
		}
	}
	return result;
}

json AnalyzerService::disk() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return diskLocked();
}

std::string AnalyzerService::exportName(const std::string &key, const std::string &name, const std::string &suffix)
{
	// Identifier first, then a file system friendly version of the name.
	std::string clean;
	for (char c : name) {
		clean += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' ? c : '_';
	}
	if (clean.size() > 40) {
		clean.resize(40);
	}
	return "sync_" + key + (clean.empty() ? "" : "_" + clean) + suffix;
}

bool AnalyzerService::sessionDir(const std::string &key, fs_path &dir, std::string &error) const
{
	const bool valid = !key.empty() && std::all_of(key.begin(), key.end(),
		[](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; });
	dir = fs::path(m_dataDir) / "sessions" / key;
	if (!valid || !fs::is_directory(dir)) {
		error = "no session " + key;
		return false;
	}
	return true;
}

json AnalyzerService::listSessions() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	json list = json::array();
	std::error_code ec;
	for (const auto &entry : fs::directory_iterator(fs::path(m_dataDir) / "sessions", ec)) {
		if (!entry.is_directory()) {
			continue;
		}
		const std::string key = entry.path().filename().string();
		json info = { { "key", key }, { "name", key }, { "tags", json::array() }, { "started", "" }, { "duration", 0.0 } };
		std::ifstream file(entry.path() / "session.json");
		if (file) {
			try {
				const json stored = json::parse(file);
				for (const char *field : { "name", "tags", "started", "input", "source", "sampleRate", "channels", "duration", "status", "recorded" }) {
					if (stored.contains(field)) {
						info[field] = stored[field];
					}
				}
				if (stored.contains("settings") && stored["settings"].contains("mode")) {
					info["mode"] = stored["settings"]["mode"];
				}
				info["pairs"] = stored.value("pairs", json::array()).size();
				const std::string audio = stored.value("audio", "");
				info["audio"] = !audio.empty() && fs::exists(audio, ec);
			} catch (const std::exception &) {
			}
		}

		uintmax_t size = 0;
		for (const auto &f : fs::directory_iterator(entry.path(), ec)) {
			if (f.is_regular_file()) {
				size += f.file_size(ec);
			}
		}
		info["size"] = size;
		const bool current = key == m_session.m_key;
		info["current"] = current;
		info["running"] = current && m_running;
		if (current) {
			info["name"] = m_session.m_name;
			info["tags"] = m_session.m_tags;
			info["duration"] = m_session.m_time.empty() ? 0.0 : m_session.m_time.back();
			info["status"] = m_status;
		}
		list.push_back(std::move(info));
	}
	std::sort(list.begin(), list.end(), [](const json &a, const json &b) { return a["key"] > b["key"]; });
	return list;
}

bool AnalyzerService::openSession(const std::string &key, std::string &error)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_running) {
		error = "stop the measurement first";
		return false;
	}
	fs_path dir;
	if (!sessionDir(key, dir, error)) {
		return false;
	}

	Session session;
	session.m_key = key;
	session.m_dir = dir.string();
	session.m_name = key;
	ServerSettings settings = m_settings;
	try {
		std::ifstream file(dir / "session.json");
		if (!file) {
			error = "session " + key + " has no session.json";
			return false;
		}
		const json info = json::parse(file);
		session.m_name = info.value("name", key);
		session.m_tags = info.value("tags", std::vector<std::string>());
		session.m_started = info.value("started", "");
		session.m_input = info.value("input", "");
		session.m_source = info.value("source", "");
		session.m_audio = info.value("audio", "");
		session.m_recorded = info.value("recorded", false);
		session.m_sampleRate = info.value("sampleRate", 0u);
		session.m_channels = info.value("channels", 0u);
		session.m_interpFactor = info.value("interp", 1u);
		if (info.contains("settings")) {
			from_json(info["settings"], settings);
		}
	} catch (const std::exception &e) {
		error = std::string("invalid session.json: ") + e.what();
		return false;
	}
	session.m_settings = settings.m_analyzer;
	std::error_code ec;
	if (!session.m_audio.empty() && !fs::exists(session.m_audio, ec)) {
		session.m_audio.clear();
	}

	// Measurements: one row per pair and time, pairs in order of appearance.
	std::ifstream csv(dir / "measurements.csv");
	std::string line;
	std::getline(csv, line);
	while (std::getline(csv, line)) {
		std::stringstream row(line);
		std::string cell;
		std::vector<double> v;
		while (std::getline(row, cell, ',')) {
			v.push_back(std::strtod(cell.c_str(), nullptr));
		}
		if (v.size() < 10) {
			continue;
		}
		const FFTSyncCheck::ChannelAssignment assignment = { std::size_t(v[1]), std::size_t(v[2]), std::size_t(v[3]), std::size_t(v[4]) };
		auto it = std::find_if(session.m_pairs.begin(), session.m_pairs.end(), [&](const Pair &p) {
			return p.m_assignment.refChannel == assignment.refChannel && p.m_assignment.refBand == assignment.refBand
				&& p.m_assignment.sigChannel == assignment.sigChannel && p.m_assignment.sigBand == assignment.sigBand;
		});
		if (it == session.m_pairs.end()) {
			session.m_pairs.push_back(Pair { assignment });
			it = session.m_pairs.end() - 1;
		}
		if (session.m_time.empty() || v[0] != session.m_time.back()) {
			session.m_time.push_back(v[0]);
		}
		it->m_delay.push_back(float(v[5]));
		it->m_confidence.push_back(float(v[6]));
		it->m_pslr.push_back(float(v[7]));
		it->m_refRms.push_back(float(v[8]));
		it->m_sigRms.push_back(float(v[9]));
	}
	if (session.m_time.empty()) {
		error = "session " + key + " has no measurements";
		return false;
	}

	session.m_id = ++m_lastSessionId;
	m_session = std::move(session);
	m_status = "Opened";
	m_stateVersion++;
	notify();
	return true;
}

bool AnalyzerService::updateSession(const std::string &key, const json &changes, std::string &error)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	fs_path dir;
	if (!sessionDir(key, dir, error)) {
		return false;
	}
	try {
		const bool hasName = changes.contains("name");
		const bool hasTags = changes.contains("tags");
		const std::string name = hasName ? changes.at("name").get<std::string>() : "";
		const std::vector<std::string> tags = hasTags ? changes.at("tags").get<std::vector<std::string>>() : std::vector<std::string>();

		if (key == m_session.m_key) {
			if (hasName) m_session.m_name = name;
			if (hasTags) m_session.m_tags = tags;
			writeSessionInfoLocked();
			m_stateVersion++;
			notify();
			return true;
		}
		std::ifstream in(dir / "session.json");
		json info = in ? json::parse(in) : json::object();
		in.close();
		if (hasName) info["name"] = name;
		if (hasTags) info["tags"] = tags;
		std::ofstream(dir / "session.json") << info.dump(2);
	} catch (const std::exception &e) {
		error = std::string("invalid change: ") + e.what();
		return false;
	}
	return true;
}

bool AnalyzerService::deleteSession(const std::string &key, std::string &error)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	fs_path dir;
	if (!sessionDir(key, dir, error)) {
		return false;
	}
	if (key == m_session.m_key) {
		if (m_running) {
			error = "stop the measurement first";
			return false;
		}
		m_session = Session();
		m_status = "Idle";
		m_stateVersion++;
		notify();
	}
	std::error_code ec;
	fs::remove_all(dir, ec);
	if (ec) {
		error = "cannot delete " + dir.string() + ": " + ec.message();
		return false;
	}
	return true;
}

bool AnalyzerService::sessionCsv(const std::string &key, std::string &csv, std::string &filename, std::string &error) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (key.empty() || key == m_session.m_key) {
		if (m_session.m_key.empty()) {
			error = "no session";
			return false;
		}
		csv = measurementsCsvLocked();
		filename = exportName(m_session.m_key, m_session.m_name, "_measurements.csv");
		return true;
	}
	fs_path dir;
	if (!sessionDir(key, dir, error)) {
		return false;
	}
	std::ifstream file(dir / "measurements.csv", std::ios::binary);
	csv.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	std::string name = key;
	std::ifstream info(dir / "session.json");
	try {
		name = info ? json::parse(info).value("name", key) : key;
	} catch (const std::exception &) {
	}
	filename = exportName(key, name, "_measurements.csv");
	return true;
}

bool AnalyzerService::sessionAudioFile(const std::string &key, std::string &path, std::string &filename, std::string &error) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::string name;
	std::error_code ec;
	if (key.empty() || key == m_session.m_key) {
		path = m_session.m_audio;
		name = m_session.m_name;
		filename = exportName(m_session.m_key, name, "_audio" + fs::path(path).extension().string());
	} else {
		fs_path dir;
		if (!sessionDir(key, dir, error)) {
			return false;
		}
		try {
			std::ifstream info(dir / "session.json");
			const json stored = json::parse(info);
			path = stored.value("audio", "");
			name = stored.value("name", key);
		} catch (const std::exception &) {
			path.clear();
		}
		filename = exportName(key, name, "_audio" + fs::path(path).extension().string());
	}
	if (path.empty() || !fs::exists(path, ec)) {
		error = "this session has no audio";
		return false;
	}
	return true;
}
