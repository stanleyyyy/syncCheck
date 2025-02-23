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
	 * @brief Structure for assigning a pair of channels to a delay measurement.
	 */
	struct ChannelAssignment
	{
		std::size_t refChannel; ///< Input channel to be used as reference.
		std::size_t refBand;    ///< Frequency band index from the reference channel.

		std::size_t sigChannel; ///< Input channel containing the signal to compare.
		std::size_t sigBand;    ///< Frequency band index from the signal channel.
	};

	/**
	 * @brief Report structure for delay statistics.
	 */
	struct DelayStatsReport {
		ChannelAssignment assignment;
		float filteredDelay;
		float filteredPSLR;
		float confidence;
		float filteredRefRMS;
		float filteredSigRMS;

		DelayStatsReport()
			: assignment()
			, filteredDelay(0.0f)
			, filteredPSLR(0.0f)
			, confidence(0.0f)
			, filteredRefRMS(0.0f)
			, filteredSigRMS(0.0f)
		{
		}

		DelayStatsReport(ChannelAssignment assignment, float filteredDelay, float filteredPSLR, float confidence, float filteredRefRMS, float filteredSigRMS)
			: assignment(assignment)
			, filteredDelay(filteredDelay)
			, filteredPSLR(filteredPSLR)
			, confidence(confidence)
			, filteredRefRMS(filteredRefRMS)
			, filteredSigRMS(filteredSigRMS)
		{
		}
	};

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
	 * @brief Calculate the cross-correlation delay between two frequency bands using FFT data.
	 *
	 * This single unified version is used in both single-channel and multi-channel modes. In
	 * single-channel mode, pass the same FFT data for both the reference and the signal.
	 *
	 * @param fft_data_ref FFT data from the reference signal
	 * @param fft_data_sig FFT data from the signal to compare
	 * @param refBand Frequency band index in the reference signal
	 * @param sigBand Frequency band index in the signal data
	 * @return A pair containing the calculated delay (in milliseconds) and PSLR (in dB)
	 */
	DelayCalculationResult calculateBandDelayFFT(const std::vector<kiss_fft_cpx>& fft_data_ref,
		const std::vector<kiss_fft_cpx>& fft_data_sig,
		std::size_t refBand, std::size_t sigBand);

	/**
	 * @brief Print comprehensive delay statistics for each channel assignment.
	 *
	 * @param expected_delays_ms Optional vector of expected delays in milliseconds for each channel assignment.
	 */
	void printDelayStats(const std::vector<float>& expected_delays_ms);

	/**
	 * @brief Process a block of interleaved input samples.
	 *
	 * This unified implementation treats single- and multi-channel processing the same way.
	 * New samples are copied into preallocated per-channel buffers; for every channel (that is used in an assignment)
	 * with at least a full block (m_config.getBlockSize()) available, the first block is windowed and transformed via FFT.
	 * The already computed FFT data is then used in calculateBandDelayFFT() for every channel assignment.
	 * @param samples Interleaved audio samples.
	 */
	void processSampleBlock(const std::vector<float>& samples);

	/**
	 * @brief Get the filtered delay of the specified channel assignment.
	 *
	 * @param assignment Index of the channel assignment.
	 * @return Filtered delay value.
	 */
	float getFilteredDelay(std::size_t assignment);

	/**
	 * @brief Get all delay statistics.
	 *
	 * Each returned tuple contains:
	 *   - ChannelAssignment: details of the channel assignment (refChannel, refBand, sigChannel, sigBand)
	 *   - Filtered delay (float)
	 *   - Confidence (float)
	 *   - Filtered PSLR (float)
	 *
	 * This makes it easier to link each delay measurement to its original channel assignment.
	 *
	 * @return Vector of tuples with channel assignment and delay statistics.
	 */
	std::vector<DelayStatsReport> getDelayStats() const;

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

	/**
	 * @brief Enable or disable multi-channel processing mode.
	 *
	 * @param numChannels If enabled, number of channels in the interleaved data.
	 */
	void setMultiChannelMode(const std::size_t numChannels);

	/**
	 * @brief Set the channel assignments for multi-channel processing.
	 *
	 * @param assignments Vector of channel assignment mappings.
	 */
	void setChannelAssignments(const std::vector<ChannelAssignment>& assignments);

	/**
	 * @brief Get the correlation data for a specific channel assignment
	 * @param assignment Channel assignment to get correlation data for
	 * @return Vector of correlation data samples
	 */
	std::vector<float> getCorrelationData(const ChannelAssignment& assignment) const;

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
	 * @brief Vector containing delay statistics for each channel assignment.
	 */
	std::vector<DelayStats> m_delayStats;

	// FFT processors
	std::unique_ptr<KissFFTWrapper> m_fftProcessor;
	std::unique_ptr<KissFFTWrapper> m_paddedFftProcessor;
	std::unique_ptr<STFTWindowGenerator> m_windowGenerator;

	std::size_t m_processingBufferFilled;
	std::vector<kiss_fft_cpx> m_correlationData;
	std::vector<kiss_fft_cpx> m_correlationDataPadded;
	std::vector<float> m_paddedTimeData;

	// Butterworth lowpass filter
	std::unique_ptr<ButterworthLowpass> m_lowpassFilter;

	// Preallocated processing buffers for each channel.
	std::vector<std::vector<float>> m_channelProcessingBuffers;

	// Temporary per-channel windowed buffers (size = blockSize).
	std::vector<std::vector<float>> m_tempWindowedBuffers;

	// Preallocated FFT output for each channel (size = blockSize/2 + 1).
	std::vector<std::vector<kiss_fft_cpx>> m_channelFFTs;

	// In this unified mode every input channel is processed.
	// (For single-channel mode, m_numInputChannels should be 1.)
	std::size_t m_numInputChannels = 1;

	/// Vector of channel assignment mappings.
	std::vector<ChannelAssignment> m_channelAssignments;

	int m_dumpCount = 0;

	// Add storage for correlation data per assignment
	std::vector<std::vector<float>> m_assignmentCorrelationData;
};
