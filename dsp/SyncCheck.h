#pragma once

#include "BandProcessor.h"
#include "SyncCheckConfig.h"
#include <cstdlib> // for free

/**
 * @brief FFTSyncCheck performs FFT-based synchronization checks across multiple frequency bands.
 */
class FFTSyncCheck
{
public:
	/**
	 * @brief Constructs the FFTSyncCheck object.
	 *
	 * @param config Configuration object for the sync checker
	 */
	explicit FFTSyncCheck(const SyncCheckConfig& config = SyncCheckConfig());

	/**
	 * @brief Destructor that cleans up Kiss FFT configurations and allocated buffers.
	 */
	~FFTSyncCheck();

	/**
	 * @brief Extract a specific frequency band from input frequency-domain data.
	 *
	 * @param input Kiss FFT complex input array.
	 * @param output Kiss FFT complex output array in which the extracted band will be stored.
	 * @param band_idx Index of the band to extract.
	 */
	void extractBand(const std::vector<kiss_fft_cpx>& input, std::vector<kiss_fft_cpx>& output, std::size_t band_idx);

	/**
	 * @brief Calculate the cross-correlation delay between two frequency bands.
	 *
	 * @param signal Input time-domain signal.
	 * @param band1 Index of the first (reference) band.
	 * @param band2 Index of the second band.
	 * @return Calculated delay in seconds.
	 */
	float calculateBandDelay(const std::vector<float> &signal, std::size_t band1, std::size_t band2);

	/**
	 * @brief Calculate the cross-correlation delay between two frequency bands using pre-computed FFT data
	 *
	 * @param fft_data Pre-computed FFT data
	 * @param band1 Index of the first (reference) band
	 * @param band2 Index of the second band
	 * @return Calculated delay in seconds
	 */
	float calculateBandDelayFFT(const std::vector<kiss_fft_cpx>& fft_data, std::size_t band1, std::size_t band2);

	/**
	 * @brief Print comprehensive delay statistics for each frequency band.
	 *
	 * @param expected_delays_ms Optional vector of expected delays in milliseconds for each band
	 */
	void printDelayStats(const std::vector<float>& expected_delays_ms = std::vector<float>());

	/**
	 * @brief Process a block of samples and update delay statistics.
	 *
	 * @param samples Vector containing time-domain samples.
	 */
	void processSampleBlock(const std::vector<float> &samples);

	/**
	 * @brief Get the filtered delay for a specific frequency band.
	 *
	 * @param band Index of the band.
	 * @return Filtered delay in seconds.
	 */
	float getFilteredDelay(std::size_t band);

	/**
	 * @brief Get the delay estimates for all frequency bands.
	 * @return Vector of delay estimates in seconds.
	 */
	std::vector<float> getDelayEstimates() const;

	/**
	 * @brief Get the sample rate.
	 * @return The sample rate.
	 */
	int getSampleRate() const { return m_config.getSampleRate(); }

	/**
	 * @brief Get the current configuration
	 * @return The current configuration
	 */
	const SyncCheckConfig& getConfig() const { return m_config; }

private:
	/**
	 * @brief Configuration object
	 */
	SyncCheckConfig m_config;

	/**
	 * @brief Vector holding frequency band definitions.
	 */
	std::vector<SyncCheckConfig::Band> m_bands;

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

			// by default use median
			std::vector<float> sorted = m_history;
			std::sort(sorted.begin(), sorted.end());
			m_filteredDelay = sorted[sorted.size() / 2];

			// Use Median Absolute Deviation (MAD) to handle outliers
			if (m_history.size() == HISTORY_SIZE) {

				// Calculate median
				std::vector<float> sorted = m_history;
				std::sort(sorted.begin(), sorted.end());
				float median = sorted[sorted.size() / 2];

				// Calculate absolute deviations from median
				std::vector<float> deviations;
				deviations.reserve(HISTORY_SIZE);
				for (float val : m_history) {
					deviations.push_back(fabsf(val - median));
				}

				// Calculate MAD (median of absolute deviations)
				std::sort(deviations.begin(), deviations.end());
				float mad = deviations[deviations.size() / 2];

				// For a normal distribution, MAD_SCALE = 1.4826 converts MAD to standard deviation
				// OUTLIER_THRESHOLD = 3.0 is the common "3-sigma rule" - data within 3 std devs
				const float MAD_SCALE = 1.4826f;     // 1/Φ^(-1)(3/4) where Φ is normal CDF
				const float OUTLIER_THRESHOLD = 3.0f; // Points >3σ from median considered outliers
				float scaled_mad = mad * MAD_SCALE;   // Convert MAD to equivalent std deviation
				std::vector<float> inliers;

				for (float val : m_history) {
					if (fabsf(val - median) <= OUTLIER_THRESHOLD * scaled_mad) {
						inliers.push_back(val);
					}
				}

				// Use median of inliers as filtered delay
				if (!inliers.empty()) {
					std::sort(inliers.begin(), inliers.end());
					m_filteredDelay = inliers[inliers.size() / 2];
				}
			}

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
	 * @brief Buffer for time-domain data used in analysis.
	 */
	std::vector<float> m_analysisTimeData;

	/**
	 * @brief Buffer for frequency-domain data used in analysis.
	 */
	std::vector<kiss_fft_cpx> m_analysisFreqData;

	/**
	 * @brief Buffer for storing frequency-domain cross-correlation data.
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
};
