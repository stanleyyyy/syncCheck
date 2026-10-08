#pragma once

#include "SyncCheck.h"
#include "SyncCheckConfig.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * @brief Settings of a sync analysis run.
 */
struct SyncAnalyzerSettings
{
	/**
	 * @brief Audio capture device (see AudioDevices) or "file://PATH" to analyze an audio file.
	 */
	std::string m_captureDevice = "default";

	/**
	 * @brief Audio output device used to monitor the captured signal. Empty disables monitoring.
	 */
	std::string m_playbackDevice = "";

	/**
	 * @brief Sample rate.
	 */
	unsigned int m_sampleRate = 48000;

	/**
	 * @brief Number of physical input channels.
	 */
	unsigned int m_channels = 2;

	/**
	 * @brief Audio period size.
	 */
	unsigned int m_periodSize = 1024;

	/**
	 * @brief Number of audio periods.
	 */
	unsigned int m_periods = 4;

	/**
	 * @brief FFT block size.
	 */
	unsigned int m_blockSize = SyncCheckConfig::DEFAULT_BLOCK_SIZE;

	/**
	 * @brief Overlap percent.
	 */
	unsigned int m_overlapPercent = SyncCheckConfig::DEFAULT_OVERLAP_PERCENT;

	/**
	 * @brief Number of frequency bands.
	 */
	unsigned int m_numBands = SyncCheckConfig::DEFAULT_NUM_BANDS;

	/**
	 * @brief Interpolation factor for spectral analysis.
	 */
	unsigned int m_interpFactor = SyncCheckConfig::DEFAULT_INTERP_FACTOR;

	/**
	 * @brief Start frequency of processing bands in Hz.
	 */
	unsigned int m_startFreq = 16000;

	/**
	 * @brief Frequency range of processing bands in Hz.
	 */
	unsigned int m_freqRange = 8000;

	/**
	 * @brief Band separation of processing bands in Hz.
	 */
	unsigned int m_bandSep = 200;

	/**
	 * @brief Stats refresh rate in seconds.
	 */
	float m_statsRefreshRate = 1.0f;

	/**
	 * @brief Audio dump enabled.
	 */
	bool m_dumpAudio = false;

	/**
	 * @brief Dump correlation data for debugging.
	 */
	bool m_dumpCorrelation = false;

	/**
	 * @brief Unified source mapping.
	 *
	 * Provide "mix" to mix all physical channels or a CSV list of mappings.
	 * Each mapping is either a single number (physical channel, defaults to band 0)
	 * or in the form "physical:band" (e.g. "0:0,1:1" assigns physical channels 0 and 1
	 * to band 0 and band 1 respectively).
	 */
	std::string m_sources = "mix";

	/**
	 * @brief When analyzing a file, deliver its samples at the real playback speed.
	 */
	bool m_realtimeFile = false;
};

/**
 * @brief Measurement result of one channel assignment.
 */
struct SyncAnalyzerResult
{
	FFTSyncCheck::DelayStatsReport m_report;

	/**
	 * @brief Current cross correlation of the assignment.
	 */
	std::vector<float> m_correlation;
};

/**
 * @brief Statistics published by the analyzer at the configured refresh rate.
 */
struct SyncAnalyzerSnapshot
{
	/**
	 * @brief Incremented with every published snapshot.
	 */
	uint64_t m_sequence = 0;

	/**
	 * @brief Amount of analyzed audio in seconds.
	 */
	float m_timestamp = 0.0f;

	unsigned int m_sampleRate = 0;
	unsigned int m_interpFactor = 0;

	/**
	 * @brief Results of all assignments, self assignments (reference compared to itself) excluded.
	 */
	std::vector<SyncAnalyzerResult> m_results;
};

/**
 * @brief Captures audio and runs FFTSyncCheck on it.
 */
class SyncAnalyzer
{
public:
	using StatsCallback = std::function<void(const SyncAnalyzerSnapshot &snapshot)>;

	/**
	 * @brief Parse a source mapping (see SyncAnalyzerSettings::m_sources) into channel assignments.
	 * @param sources Source mapping, must not be "mix".
	 * @param channels Number of physical channels, limits the number of assignments.
	 * @param assignments Receives the assignments.
	 * @param error Receives a description of a parse error.
	 * @return True on success.
	 */
	static bool parseSources(const std::string &sources, unsigned int channels,
		std::vector<FFTSyncCheck::ChannelAssignment> &assignments, std::string &error);

	/**
	 * @brief Run the analysis until stop() is called or the input ends.
	 *
	 * @param settings Analysis settings.
	 * @param onStats Called from the calling thread at the configured refresh rate.
	 * @param error Receives a description of a failure.
	 * @return True if the run ended because of stop() or end of input, false on error.
	 */
	bool run(const SyncAnalyzerSettings &settings, const StatsCallback &onStats, std::string &error);

	/**
	 * @brief Request run() to return. Safe to call from any thread and from signal handlers.
	 */
	void stop() { m_stopRequested = true; }

private:
	std::atomic<bool> m_stopRequested { false };
};
