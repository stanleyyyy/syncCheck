#pragma once

#include "kiss_fftr.h"
#include <vector>
#include <complex>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <random>
#include <stdexcept>
#include <cmath>
#include <cstdlib> // for free
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Forward declare the FFT configuration type
struct kiss_fftr_state;
typedef struct kiss_fftr_state* kiss_fftr_cfg;

// Define the deleter type
using kiss_fftr_deleter = void(*)(void*);

/**
 * @brief FFTSyncCheck performs FFT‐based synchronization checks across multiple frequency bands.
 */
class FFTSyncCheck
{
public:
	/**
	 * @brief Number of frequency bands used in analysis.
	 */
	static constexpr int NUM_BANDS = 8;

	/**
	 * @brief Block size used for analysis and test signal generation.
	 */
	static constexpr int BLOCK_SIZE = 4096;

	/**
	 * @brief Interpolation factor for delay calculations.
	 */
	static constexpr int INTERP_FACTOR = 4;

	/**
	 * @brief Constructs the FFTSyncCheck object.
	 *
	 * @param fft_size FFT size to use for analysis.
	 * @param sample_rate Sampling rate of the signal.
	 */
	FFTSyncCheck(int fft_size = BLOCK_SIZE, int sample_rate = 48000);

	/**
	 * @brief Destructor that cleans up Kiss FFT configurations and allocated buffers.
	 */
	~FFTSyncCheck();

	/**
	 * @brief Extract a specific frequency band from input frequency‐domain data.
	 *
	 * @param input Kiss FFT complex input array.
	 * @param output Kiss FFT complex output array in which the extracted band will be stored.
	 * @param band_idx Index of the band to extract.
	 */
	void extractBand(const std::vector<kiss_fft_cpx>& input, std::vector<kiss_fft_cpx>& output, int band_idx);

	/**
	 * @brief Calculate the cross‐correlation delay between two frequency bands.
	 *
	 * @param signal Input time‐domain signal.
	 * @param band1 Index of the first (reference) band.
	 * @param band2 Index of the second band.
	 * @return Calculated delay in seconds.
	 */
	float calculateBandDelay(const std::vector<float> &signal, int band1, int band2);

	/**
	 * @brief Calculate the cross‐correlation delay between two frequency bands using pre-computed FFT data
	 *
	 * @param fft_data Pre-computed FFT data
	 * @param band1 Index of the first (reference) band
	 * @param band2 Index of the second band
	 * @return Calculated delay in seconds
	 */
	float calculateBandDelayFFT(const std::vector<kiss_fft_cpx>& fft_data, int band1, int band2);

	/**
	 * @brief Print comprehensive delay statistics for each frequency band.
	 */
	void printDelayStats();

	/**
	 * @brief Process a block of samples and update delay statistics.
	 *
	 * @param samples Vector containing time‐domain samples.
	 */
	void processSampleBlock(const std::vector<float> &samples);

	/**
	 * @brief Get the filtered delay for a specific frequency band.
	 *
	 * @param band Index of the band.
	 * @return Filtered delay in seconds.
	 */
	float getFilteredDelay(int band);

	/**
	 * @brief Generate a test signal with specified delays for all bands using overlap‐add processing.
	 *
	 * @param duration Duration of the test signal in seconds.
	 * @param delays_ms Vector containing delays (in milliseconds) for each band.
	 * @param verbose If true, prints progress and quality metrics.
	 * @return Generated mixed test signal.
	 */
	std::vector<float> generateTestSignal(float duration, std::vector<float> &delays_ms, bool verbose = true);

	/**
	 * @brief Sampling rate.
	 */
	const int m_sampleRate;

	/**
	 * @brief FFT size.
	 */
	const int m_fftSize;

private:
	/**
	 * @brief Structure representing a frequency band.
	 */
	struct Band
	{
		int m_startBin; ///< Starting FFT bin index.
		int m_endBin;	///< Ending FFT bin index.
	};

	/**
	 * @brief Vector holding frequency band definitions.
	 */
	std::vector<Band> m_bands;

	/**
	 * @brief Structure for tracking delay measurements and statistics.
	 */
	struct DelayStats
	{
		static const int HISTORY_SIZE = 10; ///< Number of measurements to keep.
		std::vector<float> m_history;		///< History of delay measurements.
		float m_filteredDelay;				///< Filtered (median) delay.
		float m_confidence;					///< Confidence metric based on variance.

		DelayStats()
			: m_filteredDelay(0.0f)
			, m_confidence(0.0f)
		{
			m_history.reserve(HISTORY_SIZE);
		}

		/**
		 * @brief Update delay statistics with a new measurement.
		 *
		 * @param new_delay New delay measurement in seconds.
		 */
		void update(float new_delay)
		{
			if (m_history.size() >= HISTORY_SIZE) {
				m_history.erase(m_history.begin());
			}
			m_history.push_back(new_delay);

			// Calculate median.
			std::vector<float> sorted = m_history;
			std::sort(sorted.begin(), sorted.end());
			m_filteredDelay = sorted[sorted.size() / 2];

			// Calculate confidence based on variance.
			float sum = 0.0f;
			for (float d : m_history) {
				float diff = d - m_filteredDelay;
				sum += diff * diff;
			}
			float variance = sum / m_history.size();
			m_confidence = 1.0f / (1.0f + sqrtf(variance));
		}
	};

	/**
	 * @brief Vector containing delay statistics for each frequency band.
	 */
	std::vector<DelayStats> m_delayStats;

	/**
	 * @brief Kiss FFT configuration for forward (real-to-complex) analysis.
	 */
	std::unique_ptr<kiss_fftr_state, kiss_fftr_deleter> m_analysisForwardCfg;

	/**
	 * @brief Kiss FFT configuration for inverse (complex-to-real) analysis.
	 */
	std::unique_ptr<kiss_fftr_state, kiss_fftr_deleter> m_analysisInverseCfg;

	/**
	 * @brief Buffer for time‐domain data used in analysis.
	 */
	std::vector<float> m_analysisTimeData;

	/**
	 * @brief Buffer for frequency‐domain data used in analysis.
	 */
	std::vector<kiss_fft_cpx> m_analysisFreqData;

	/**
	 * @brief Buffer for storing frequency‐domain cross‐correlation data.
	 */
	std::vector<kiss_fft_cpx> m_correlationData;

	/**
	 * @brief Padded FFT configurations for delay calculations
	 */
	std::unique_ptr<kiss_fftr_state, kiss_fftr_deleter> m_paddedForwardCfg;
	std::unique_ptr<kiss_fftr_state, kiss_fftr_deleter> m_paddedInverseCfg;
	std::vector<float> m_paddedTimeData;
	std::vector<kiss_fft_cpx> m_paddedCorrData;
	std::vector<kiss_fft_cpx> m_paddedFreqData;

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
		void print() const
		{
			printf("Signal Quality Metrics:\n");
			printf("  SNR: %.2f dB\n", m_snr);
			printf("  Band Isolation: %.2f dB\n", m_bandIsolation);
			printf("  Phase Coherence: %.2f\n", m_phaseCoherence);
		}
	};

	/**
	 * @brief Inner class for processing a specific frequency band using the overlap‐add method.
	 */
	class BandProcessor
	{
	public:
		/**
		 * @brief Constructs a BandProcessor for a given frequency band.
		 *
		 * @param block_size Size of the processing block.
		 * @param overlap_size Number of samples to overlap.
		 * @param band Reference to the frequency band to process.
		 */
		BandProcessor(int block_size, int overlap_size, const Band &band, int sample_rate);

		/**
		 * @brief Copy constructor.
		 */
		BandProcessor(const BandProcessor &other);

		/**
		 * @brief Move constructor.
		 */
		BandProcessor(BandProcessor &&other) noexcept;

		/**
		 * @brief Destructor that cleans up Kiss FFT resources.
		 */
		~BandProcessor();

		/**
		 * @brief Assignment operator.
		 */
		BandProcessor &operator=(const BandProcessor &other);

		/**
		 * @brief Move assignment operator.
		 */
		BandProcessor &operator=(BandProcessor &&other) noexcept;

		/**
		 * @brief Process a block of input samples and produce an output block.
		 *
		 * @param input Reference to input samples.
		 * @param output Reference to output samples.
		 */
		void processBlock(const std::vector<float>& input, std::vector<float>& output);

		/**
		 * @brief Process a block with pre-computed FFT data
		 * @param fft_data Pre-computed FFT data
		 * @param output Output buffer of size m_blockSize
		 */
		void processBlockFFT(const std::vector<kiss_fft_cpx>& fft_data, std::vector<float>& output);

		/**
		 * @brief Process a block of samples with frequency shifting
		 * @param input Reference to input buffer of size m_blockSize
		 * @param output Reference to output buffer of size m_blockSize
		 */
		void processBlockWithShift(const std::vector<float>& input, std::vector<float>& output);

		/**
		 * @brief Process a block with frequency shifting using pre-computed FFT data
		 * @param fft_data Pre-computed FFT data
		 * @param output Reference to output buffer of size m_blockSize
		 */
		void processBlockWithShiftFFT(const std::vector<kiss_fft_cpx>& fft_data, std::vector<float>& output);

		/**
		 * @brief Apply window function to input data
		 * @param input Reference to input buffer
		 * @param output Reference to output buffer
		 * @param size Buffer size
		 */
		void applyWindow(const std::vector<float>& input, std::vector<float>& output, int size) const;

		/**
		 * @brief Get the band isolation metric in dB.
		 *
		 * @return Band isolation in dB.
		 */
		float getBandIsolation() const;

		/**
		 * @brief Allocate resources for the band processor.
		 */
		void allocateResources();

		/**
		 * @brief Free resources for the band processor.
		 */
		void freeResources();

		/**
		 * @brief Sets the target frequency for shifting the band
		 * @param targetFreqHz Target center frequency in Hz
		 */
		void setFrequencyShift(float targetFreqHz);

	private:
		/**
		 * @brief Kiss FFT configuration for forward (real-to-complex) transform.
		 */
		std::unique_ptr<kiss_fftr_state, kiss_fftr_deleter> m_forwardCfg;

		/**
		 * @brief Kiss FFT configuration for inverse (complex-to-real) transform.
		 */
		std::unique_ptr<kiss_fftr_state, kiss_fftr_deleter> m_inverseCfg;

		/**
		 * @brief Buffer for time‐domain data.
		 */
		std::vector<float> m_timeData;

		/**
		 * @brief Buffer for frequency‐domain data.
		 */
		std::vector<kiss_fft_cpx> m_freqData;

		/**
		 * @brief Working buffer for band-filtered data.
		 */
		std::vector<kiss_fft_cpx> m_workingFreqData;

		/**
		 * @brief Reference to the associated frequency band.
		 */
		Band m_band;

		/**
		 * @brief Buffer for overlap‐add.
		 */
		std::vector<float> m_overlapBuffer;

		/**
		 * @brief Processing block size.
		 */
		int m_blockSize;

		/**
		 * @brief Overlap size between blocks.
		 */
		int m_overlapSize;

		/**
		 * @brief Hann window.
		 */
		std::vector<float> m_window;

		/**
		 * @brief Accumulated power within the band.
		 */
		double m_inBandPower;

		/**
		 * @brief Accumulated power outside the band.
		 */
		double m_outBandPower;

		/**
		 * @brief Sample rate.
		 */
		int m_sampleRate;

		float m_shiftFreq;           // Frequency shift amount in Hz
		float m_phaseAccumulator;    // Accumulated phase for continuous modulation
		std::vector<float> m_modulatorBuffer;  // Buffer for modulation waveform
	};
};
