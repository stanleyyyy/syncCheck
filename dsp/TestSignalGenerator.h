#pragma once

#include "SyncCheckConfig.h"
#include "BandProcessor.h"
#include "DelayLine.h"
#include "SyncSignalGenerator.h"
#include <vector>
#include <random>
#include <memory>
#include "WavFile.h"

class TestSignalGenerator
{
public:
	/**
	 * @brief Structure representing signal quality metrics.
	 */
	struct QualityMetrics
	{
		float m_snr;			///< Signal-to-noise ratio (dB).
		float m_bandIsolation;	///< dB isolation between bands.
		float m_phaseCoherence; ///< Phase coherence metric.

		/**
		 * @brief Print the quality metrics.
		 */
		void print() const;
	};

	/**
	 * @brief Constructor
	 * @param config Configuration for the test signal generator
	 */
	explicit TestSignalGenerator(const SyncCheckConfig& config);

	/**
	 * @brief Destructor
	 */
	~TestSignalGenerator();

	/**
	 * @brief Initialize block-based signal generation
	 * @param delays Vector of delays in milliseconds for each band
	 * @throws std::runtime_error if delays array size doesn't match number of bands
	 */
	void init(std::vector<float>& delays);

	/**
	 * @brief Enable or disable dumping of band signals to WAV files
	 * @param enable Whether to enable WAV file dumping
	 * @param prefix Prefix for WAV file names (will be appended with _bandX.wav)
	 * @return True if WAV files were successfully opened, false otherwise
	 */
	bool enableWavDump(bool enable, const std::string& prefix = "band");

	/**
	 * @brief Generate one block of test signal
	 * @param output Buffer to fill with the next block of samples
	 */
	void renderAudioBlock(std::vector<float>& output);

	/**
	 * @brief Generate a complete test signal with specified delays for each band
	 * @param duration Duration of the test signal in seconds
	 * @param delays Vector of delays in milliseconds for each band
	 * @param verbose Whether to print progress and metrics
	 * @return Generated test signal
	 */
	std::vector<float> renderTestSignal(float duration, std::vector<float>& delays, bool verbose = false);

	/**
	 * @brief Get the current quality metrics
	 * @return Current quality metrics
	 */
	const QualityMetrics& getQualityMetrics() const { return m_metrics; }

private:
	/**
	 * @brief Generate reference signal for the given number of frames
	 * @param output Buffer to fill with reference signal
	 * @param numFrames Number of frames to generate
	 */
	void generateReferenceSignal(std::vector<float>& output, std::size_t numFrames);

	/**
	 * @brief Close all open WAV files
	 */
	void closeWavFiles();

private:
	SyncCheckConfig m_config;
	std::unique_ptr<SyncSignalGenerator<float>> m_syncSignalGenerator;
	std::vector<BandProcessor> m_bandProcessors;
	std::unique_ptr<HilbertShifter> m_hilbertShifterBackground;
	std::unique_ptr<HilbertShifter> m_hilbertShifterReference;
	std::vector<std::unique_ptr<DelayLine>> m_delayLines;
	std::vector<int> m_delayFrames;
	std::mt19937 m_rng;
	std::uniform_real_distribution<float> m_dist;
	std::size_t m_currentSample;
	QualityMetrics m_metrics;
	bool m_isInitialized;
	bool m_wavDumpEnabled;

	std::unique_ptr<WavWriter> m_referenceFile;
	std::vector<std::unique_ptr<WavWriter>> m_wavFiles;

	// Buffers to maintain history between calls
	std::vector<float> m_backgroundBuffer;
	std::vector<float> m_referenceBuffer;
	std::vector<std::vector<float>> m_bandOutputs;

	std::size_t m_processingBufferFilled;
	std::vector<float> m_processingBuffer;
};