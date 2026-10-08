#pragma once

#include "SyncCheck.h"
#include "SyncCheckConfig.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * @brief What the analyzer compares.
 */
enum class SyncAnalyzerMode
{
	/**
	 * @brief Use the source mapping (m_sources): "mix" compares frequency shifted bands of a
	 * band test signal, a channel list compares the listed channels/bands (default).
	 */
	Sources,

	/**
	 * @brief Compare the reference channel with the other input channels over one wide
	 * frequency range (m_minFreq - m_maxFreq). Works with any wideband signal.
	 */
	Channels,
};

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

	/**
	 * @brief Measurement mode.
	 */
	SyncAnalyzerMode m_mode = SyncAnalyzerMode::Sources;

	/**
	 * @brief Channel mode: reference channel.
	 */
	unsigned int m_referenceChannel = 0;

	/**
	 * @brief Channel mode: channels compared with the reference. Empty means all other channels.
	 */
	std::vector<unsigned int> m_measuredChannels;

	/**
	 * @brief Channel mode: analyzed frequency range in Hz.
	 */
	unsigned int m_minFreq = 200;
	unsigned int m_maxFreq = 20000;

	/**
	 * @brief Record the captured audio to this 16-bit WAV file, readable while recording. Empty disables it.
	 */
	std::string m_recordFile;
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

	/**
	 * @brief Amount of analyzed audio in frames.
	 */
	uint64_t m_frames = 0;

	unsigned int m_sampleRate = 0;
	unsigned int m_interpFactor = 0;
	unsigned int m_channels = 0;
	SyncAnalyzerMode m_mode = SyncAnalyzerMode::Sources;

	/**
	 * @brief Results of all assignments, self assignments (reference compared to itself) excluded.
	 */
	std::vector<SyncAnalyzerResult> m_results;
};

/**
 * @brief Captures audio and runs FFTSyncCheck on it.
 *
 * Shared by the command line tool and the server.
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

	/**
	 * @brief Check settings that do not depend on the input (block size, overlap, bands, ...).
	 * @return True if valid, otherwise error describes the problem.
	 */
	static bool validate(const SyncAnalyzerSettings &settings, std::string &error);

	/**
	 * @brief Check the settings against an input and translate channel mode into a source mapping.
	 * @param settings Requested settings.
	 * @param channels Number of input channels.
	 * @param sampleRate Input sample rate.
	 * @param resolved Receives the settings run() works with.
	 * @param error Receives a description of a problem.
	 * @return True if the settings can be used with this input.
	 */
	static bool resolveSettings(const SyncAnalyzerSettings &settings, unsigned int channels, unsigned int sampleRate,
		SyncAnalyzerSettings &resolved, std::string &error);

	/**
	 * @brief Analyze the single block of an audio file that ends at endFrame.
	 *
	 * Used to inspect the correlation at any time of a recording. The results are not filtered.
	 *
	 * @param path Audio file (may still be recorded by WavRecorder).
	 * @param settings Analysis settings; the capture device is ignored.
	 * @param endFrame Last frame of the analyzed block.
	 * @param snapshot Receives the results.
	 * @param error Receives a description of a failure.
	 * @return True on success.
	 */
	static bool analyzeBlock(const std::string &path, const SyncAnalyzerSettings &settings, uint64_t endFrame,
		SyncAnalyzerSnapshot &snapshot, std::string &error);

private:
	struct CheckerSetup
	{
		SyncCheckConfig m_config;
		bool m_multichannel = false;
		unsigned int m_procChannels = 1;
		std::vector<FFTSyncCheck::ChannelAssignment> m_assignments;
		std::vector<FFTSyncCheck::ChannelAssignment> m_checkerAssignments;
	};

	static bool setupChecker(const SyncAnalyzerSettings &settings, unsigned int sampleRate, unsigned int channels,
		CheckerSetup &setup, std::string &error);
	static void fillProcessingBuffer(const CheckerSetup &setup, const std::vector<float> &inputBuffer, unsigned int channels,
		std::vector<float> &processingBuffer);
	static void collectResults(const CheckerSetup &setup, const FFTSyncCheck &syncChecker, SyncAnalyzerSnapshot &snapshot);

	std::atomic<bool> m_stopRequested { false };
};
