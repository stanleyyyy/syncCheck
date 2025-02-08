#pragma once

#include <vector>

/**
 * @brief Configuration class for FFTSyncCheck system
 */
class SyncCheckConfig
{
public:

	/**
	 * @brief Structure representing a frequency band.
	 */
	struct Band
	{
		std::size_t m_startBin; ///< Starting FFT bin index.
		std::size_t m_endBin;	///< Ending FFT bin index.
	};

	/**
	 * @brief Number of frequency bands used in analysis.
	 */
	static constexpr int DEFAULT_NUM_BANDS = 8;

	/**
	 * @brief Interpolation factor for delay calculations.
	 */
	static constexpr int DEFAULT_INTERP_FACTOR = 1;

	/**
	 * @brief Sampling rate of the signal.
	 */
	static constexpr int DEFAULT_SAMPLE_RATE = 48000;

	/**
	 * @brief Block size used for analysis and test signal generation.
	 */
	static constexpr int DEFAULT_BLOCK_SIZE = 8192;

	/**
	 * @brief Overlap size for analysis and test signal generation.
	 * The window in use (Hann or Bartlett) was tested for overlaps of 50% and 75%
	 */

	static constexpr int DEFAULT_OVERLAP_PERCENT = 50;

	/**
	 * @brief Constructor with default configuration
	 *
	 * @param fft_size FFT size to use for analysis
	 * @param sample_rate Sampling rate of the signal
	 * @param num_bands Number of frequency bands to use
	 */
	SyncCheckConfig(std::size_t fft_size = DEFAULT_BLOCK_SIZE, std::size_t overlap = DEFAULT_OVERLAP_PERCENT,
					std::size_t sample_rate = DEFAULT_SAMPLE_RATE,
					std::size_t num_bands = DEFAULT_NUM_BANDS, std::size_t interp_factor = DEFAULT_INTERP_FACTOR);

	/**
	 * @brief Get the block size. Also equal to the FFT size.
	 * @return The block size
	 */
	std::size_t getBlockSize() const { return m_fftSize; }

	/**
	 * @brief Get the overlap size
	 * @return The overlap size
	 */
	std::size_t getOverlapSize() const { return m_overlapSize; }

	/**
	 * @brief Get the hop size
	 * @return The hop size
	 */
	std::size_t getHopSize() const { return m_fftSize - m_overlapSize; }

	/**
	 * @brief Get the sample rate
	 * @return The sample rate
	 */
	std::size_t getSampleRate() const { return m_sampleRate; }

	/**
	 * @brief Get the number of bands
	 * @return The number of bands
	 */
	std::size_t getNumBands() const { return m_numBands; }

	/**
	 * @brief Get the interpolation factor
	 * @return The interpolation factor
	 */
	std::size_t getInterpFactor() const { return m_interpFactor; }

	/**
	 * @brief Get all band definitions
	 * @return Vector of band definitions
	 */
	const std::vector<SyncCheckConfig::Band>& getBands() const { return m_bands; }

	/**
	 * @brief Set custom band configuration
	 * @param bands Vector of band definitions
	 */
	void setBands(const std::vector<SyncCheckConfig::Band>& bands);

	/**
	 * @brief Set the interpolation factor
	 * @param factor New interpolation factor
	 */
	void setInterpFactor(std::size_t factor) { m_interpFactor = factor; }

private:
	std::size_t m_fftSize;		///< FFT size for analysis
	std::size_t m_overlapSize;	///< Overlap size for analysis and test signal generation
	std::size_t m_sampleRate;	///< Sampling rate of the signal
	std::size_t m_numBands;		///< Number of frequency bands
	std::size_t m_interpFactor;	///< Interpolation factor for delay calculations
	std::vector<SyncCheckConfig::Band> m_bands;	///< Band definitions

	/**
	 * @brief Initialize default band configuration
	 */
	void initDefaultBands();
};