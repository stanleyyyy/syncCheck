#pragma once

#include "KissFFTWrapper.h"
#include "STFTWindowGenerator.h"
#include "SyncCheckConfig.h"
#include "DelayStats.h"
#include "ButterworthLowpass.h"
#include <vector>
#include <memory>

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
	 * @return Calculated delay in milliseconds and PSLR in dB
	 */
	std::pair<float, float> calculateBandDelayFFT(const std::vector<kiss_fft_cpx>& fft_data, std::size_t band1, std::size_t band2);

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
	 * @brief Get all delay statistics
	 * @return Vector of pairs containing filtered delay and confidence for each band
	 */
	std::vector<std::tuple<float, float, float>> getDelayStats() const;

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

	/**
		@brief Computes the smoothed envelope of the input signal.

		The function computes the absolute value of the input data and then applies a low-pass
		Butterworth filter with zero-phase filtering. The sampling interval is derived from the
		times vector.

		@param data The input signal.
		@return The smoothed envelope.
	*/
	std::vector<float> getSmoothEnvelope(const std::vector<float>& data);

	/**
	 * @brief Compute the PSLR (Peak-to-Side-Lobe Ratio) of a correlation vector.
	 *
	 * @param corr The correlation vector.
	 * @param exclusion_radius The number of samples around the main peak to exclude.
	 * @return The PSLR in dB.
	 */
	float computePSLR(const std::vector<float>& corr, int exclusion_radius);

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
	 * @brief Vector containing delay statistics for each frequency band.
	 */
	std::vector<DelayStats> m_delayStats;

	// FFT processors
	std::unique_ptr<KissFFTWrapper> m_fftProcessor;
	std::unique_ptr<KissFFTWrapper> m_paddedFftProcessor;
	std::unique_ptr<STFTWindowGenerator> m_windowGenerator;

	// Working buffers
	std::size_t m_processingBufferFilled;
	std::vector<float> m_processingBuffer;
	std::vector<float> m_windowedBuffer;
	std::vector<kiss_fft_cpx> m_correlationData;
	std::vector<kiss_fft_cpx> m_correlationDataPadded;
	std::vector<float> m_paddedTimeData;

	// Butterworth lowpass filter
	std::unique_ptr<ButterworthLowpass> m_lowpassFilter;
};
