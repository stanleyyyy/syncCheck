#include "SyncAnalyzer.h"
#include "TestSignalGenerator.h"
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

int main()
{
	Logger::getInstance().setLogLevel(LOG_WARNING);

	testMixMode();
	testMultichannelMode();

	if (g_failures) {
		DBG_ERROR(g_failures << " check(s) failed");
		return 1;
	}
	DBG_WARN("All checks passed");
	return 0;
}
