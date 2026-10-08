#include "SyncAnalyzer.h"
#include "TestSignalGenerator.h"
#include "NoiseSignalGenerator.h"
#include "WavFile.h"
#include "Logger.h"
#include <cmath>
#include <cstdio>

static int g_failures = 0;

static void check(bool condition, const std::string &message)
{
	if (!condition) {
		DBG_ERROR("FAILED: " << message);
		g_failures++;
	}
}

/**
 * @brief Render the generator test signal (8 bands, 16..24 kHz) with the given per band delays.
 */
static std::vector<float> renderTestSignal(std::vector<float> delays, float duration)
{
	SyncCheckConfig config(8192, 50, 48000, 8, 1);
	config.initBands(16000, 8000, 200);
	TestSignalGenerator generator(config);
	return generator.renderTestSignal(duration, delays);
}

static SyncAnalyzerSnapshot analyze(const SyncAnalyzerSettings &settings)
{
	SyncAnalyzer analyzer;
	SyncAnalyzerSnapshot last;
	std::string error;
	const bool ok = analyzer.run(settings, [&](const SyncAnalyzerSnapshot &snapshot) { last = snapshot; }, error);
	check(ok, "analyzer run failed: " + error);
	return last;
}

static SyncAnalyzerSettings defaultSettings(const std::string &file)
{
	SyncAnalyzerSettings settings;
	settings.m_captureDevice = "file://" + file;
	settings.m_numBands = 8;
	settings.m_interpFactor = 1;
	settings.m_startFreq = 16000;
	settings.m_freqRange = 8000;
	settings.m_bandSep = 200;
	settings.m_statsRefreshRate = 1.0f;
	return settings;
}

/**
 * @brief Mix mode: every band of a mono signal is delayed against band 0.
 */
static void testMixMode()
{
	std::vector<float> delays = { 0.0f, -0.3f, 1.0f, -0.5f, 2.0f, -1.0f, 0.7f, -1.3f };
	const std::vector<float> signal = renderTestSignal(delays, 10.0f);

	const std::string file = "syncAnalyzerTest_mix.wav";
	{
		WavWriter writer;
		check(writer.open(file, 1, 48000), "cannot write " + file);
		writer.writeFrames(signal.data(), signal.size());
	}

	const SyncAnalyzerSnapshot snapshot = analyze(defaultSettings(file));
	check(snapshot.m_results.size() == delays.size() - 1, "unexpected number of results in mix mode");
	for (const auto &result : snapshot.m_results) {
		const auto &report = result.m_report;
		const float expected = delays[report.assignment.sigBand];
		DBG_INFO("mix band " << report.assignment.sigBand << ": " << report.filteredDelay << " ms (expected " << expected
			<< " ms, confidence " << report.confidence << ")");
		check(std::fabs(report.filteredDelay - expected) < 0.1f, "mix band " + std::to_string(report.assignment.sigBand) + " delay");
		check(report.confidence > 0.9f, "mix band " + std::to_string(report.assignment.sigBand) + " confidence");
		check(!result.m_correlation.empty(), "missing correlation data");
	}
	std::remove(file.c_str());
}

/**
 * @brief Multichannel mode: channel 1 is channel 0 delayed by 1 ms, channels 2 and 3 are unmapped.
 */
static void testMultichannelMode()
{
	std::vector<float> delays(8, 0.0f);
	const std::vector<float> signal = renderTestSignal(delays, 10.0f);

	const unsigned int channels = 4;
	const std::size_t delayFrames = 48;
	std::vector<float> interleaved(signal.size() * channels, 0.0f);
	for (std::size_t i = 0; i < signal.size(); i++) {
		interleaved[i * channels + 0] = signal[i];
		interleaved[i * channels + 1] = i >= delayFrames ? signal[i - delayFrames] : 0.0f;
	}

	const std::string file = "syncAnalyzerTest_multichannel.wav";
	{
		WavWriter writer;
		check(writer.open(file, channels, 48000), "cannot write " + file);
		writer.writeFrames(interleaved.data(), signal.size());
	}

	SyncAnalyzerSettings settings = defaultSettings(file);
	settings.m_sources = "0,1";
	const SyncAnalyzerSnapshot snapshot = analyze(settings);
	check(snapshot.m_results.size() == 1, "unexpected number of results in multichannel mode");
	for (const auto &result : snapshot.m_results) {
		const auto &report = result.m_report;
		DBG_INFO("channel " << report.assignment.sigChannel << ": " << report.filteredDelay << " ms (expected 1 ms, confidence "
			<< report.confidence << ")");
		check(std::fabs(report.filteredDelay - 1.0f) < 0.1f, "multichannel delay");
	}
	std::remove(file.c_str());
}

/**
 * @brief Source mappings that are not 0,1,2,... (e.g. "2,3") compare the right physical channels.
 */
static void testNonIdentityMapping()
{
	const std::vector<float> delays = { 0.0f, 1.0f, -2.5f, 0.375f };
	const std::vector<float> signal = generateNoiseTestSignal(4, 48000, 10.0f, delays);

	const std::string file = "syncAnalyzerTest_mapping.wav";
	{
		WavWriter writer;
		check(writer.open(file, 4, 48000), "cannot write " + file);
		writer.writeFrames(signal.data(), signal.size() / 4);
	}

	SyncAnalyzerSettings settings = defaultSettings(file);
	settings.m_numBands = 1;
	settings.m_startFreq = 200;
	settings.m_freqRange = 19800;
	settings.m_sources = "2,3";
	const SyncAnalyzerSnapshot snapshot = analyze(settings);
	check(snapshot.m_results.size() == 1, "unexpected number of results for mapping 2,3");
	for (const auto &result : snapshot.m_results) {
		const auto &report = result.m_report;
		DBG_INFO("channel " << report.assignment.refChannel << " -> " << report.assignment.sigChannel << ": "
			<< report.filteredDelay << " ms (expected 2.875 ms)");
		check(report.assignment.refChannel == 2 && report.assignment.sigChannel == 3, "mapping 2,3 channels");
		check(std::fabs(report.filteredDelay - 2.875f) < 0.02f, "mapping 2,3 delay");
		check(!result.m_correlation.empty(), "missing correlation data");
	}
	std::remove(file.c_str());
}

/**
 * @brief Channel mode compares the reference with all other channels over a wide band.
 */
static void testChannelMode()
{
	const std::vector<float> delays = { 0.0f, 1.0f, -2.5f, 0.375f };
	const std::vector<float> signal = generateNoiseTestSignal(4, 48000, 10.0f, delays);

	const std::string file = "syncAnalyzerTest_channels.wav";
	{
		WavWriter writer;
		check(writer.open(file, 4, 48000), "cannot write " + file);
		writer.writeFrames(signal.data(), signal.size() / 4);
	}

	SyncAnalyzerSettings settings;
	settings.m_captureDevice = "file://" + file;
	settings.m_mode = SyncAnalyzerMode::Channels;
	SyncAnalyzerSnapshot snapshot = analyze(settings);
	check(snapshot.m_results.size() == 3, "channel mode: expected 3 results");
	for (const auto &result : snapshot.m_results) {
		const auto &report = result.m_report;
		const float expected = delays[report.assignment.sigChannel];
		DBG_INFO("channel " << report.assignment.sigChannel << ": " << report.filteredDelay << " ms (expected " << expected << " ms)");
		check(report.assignment.refChannel == 0, "channel mode: reference");
		check(std::fabs(report.filteredDelay - expected) < 0.02f, "channel mode delay of channel " + std::to_string(report.assignment.sigChannel));
		check(!result.m_correlation.empty(), "channel mode: missing correlation data");
	}

	// Other reference, subset of channels
	settings.m_referenceChannel = 3;
	settings.m_measuredChannels = { 1 };
	snapshot = analyze(settings);
	check(snapshot.m_results.size() == 1 && std::fabs(snapshot.m_results[0].m_report.filteredDelay - 0.625f) < 0.02f,
		"channel mode 3 -> 1 delay");

	// Single block analysis at 5 s
	settings.m_referenceChannel = 0;
	settings.m_measuredChannels.clear();
	std::string error;
	check(SyncAnalyzer::analyzeBlock(file, settings, 5 * 48000, snapshot, error), "analyzeBlock failed: " + error);
	check(snapshot.m_results.size() == 3, "analyzeBlock: expected 3 results");
	for (const auto &result : snapshot.m_results) {
		const float expected = delays[result.m_report.assignment.sigChannel];
		check(std::fabs(result.m_report.filteredDelay - expected) < 0.02f, "analyzeBlock delay");
	}

	// A mono input cannot be measured in channel mode
	const std::string mono = "syncAnalyzerTest_mono.wav";
	{
		WavWriter writer;
		writer.open(mono, 1, 48000);
		writer.writeFrames(signal.data(), 48000);
	}
	settings.m_captureDevice = "file://" + mono;
	SyncAnalyzer analyzer;
	check(!analyzer.run(settings, nullptr, error), "channel mode must reject a mono input");
	std::remove(mono.c_str());
	std::remove(file.c_str());
}

/**
 * @brief Recordings are complete and readable; mix mode needs at least 2 bands.
 */
static void testRecordingAndValidation()
{
	const std::vector<float> signal = generateNoiseTestSignal(2, 48000, 3.0f, { 0.0f, 0.5f });
	const std::string file = "syncAnalyzerTest_record_in.wav";
	const std::string recording = "syncAnalyzerTest_record_out.wav";
	{
		WavWriter writer;
		writer.open(file, 2, 48000);
		writer.writeFrames(signal.data(), signal.size() / 2);
	}

	SyncAnalyzerSettings settings;
	settings.m_captureDevice = "file://" + file;
	settings.m_mode = SyncAnalyzerMode::Channels;
	settings.m_recordFile = recording;
	analyze(settings);

	WavReader reader;
	check(reader.open(recording) && reader.getChannels() == 2 && reader.getLengthInFrames() == 3 * 48000, "recording is incomplete");
	reader.close();

	SyncAnalyzerSettings resolved;
	std::string error;
	settings = SyncAnalyzerSettings();
	settings.m_numBands = 1;
	check(!SyncAnalyzer::resolveSettings(settings, 2, 48000, resolved, error), "mix mode must reject a single band");

	std::remove(file.c_str());
	std::remove(recording.c_str());
}

int main()
{
	Logger::getInstance().setLogLevel(LOG_WARNING);

	testMixMode();
	testMultichannelMode();
	testNonIdentityMapping();
	testChannelMode();
	testRecordingAndValidation();

	if (g_failures) {
		DBG_ERROR(g_failures << " check(s) failed");
		return 1;
	}
	DBG_WARN("All checks passed");
	return 0;
}
