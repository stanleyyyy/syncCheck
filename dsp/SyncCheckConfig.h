#pragma once

#include <vector>
#include <cmath>

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
		double m_startFreq;  // Start frequency in Hz
		double m_endFreq;    // End frequency in Hz

		/**
		 * @brief Compare two Band objects
		 * @param other The other Band object to compare with
		 * @return True if the objects are equal, false otherwise
		 */
		bool operator == (const Band &other) const;

		/**
		 * @brief Compare two Band objects
		 * @param other The other Band object to compare with
		 * @return True if the objects are not equal, false otherwise
		 */
		bool operator != (const Band &other) const { return !(*this == other); }

		/**
		 * @brief Convert frequency to bin
		 * @param freq Frequency in Hz
		 * @param fftSize FFT size
		 * @param sampleRate Sample rate
		 * @return Bin index
		 */
		std::size_t freqToBin(double freq, std::size_t fftSize, std::size_t sampleRate) const
		{
			// Ensure frequency doesn't exceed Nyquist frequency
			double nyquist = sampleRate / 2.0;
			double clampedFreq = std::min(freq, nyquist);
			return std::round(clampedFreq * fftSize / sampleRate);
		}

		/**
		 * @brief Get start bin index
		 * @param fftSize FFT size
		 * @param sampleRate Sample rate
		 * @return Bin index
		 */
		std::size_t getStartBin(std::size_t fftSize, std::size_t sampleRate) const
		{
			return freqToBin(m_startFreq, fftSize, sampleRate);
		}

		/**
		 * @brief Get end bin index
		 * @param fftSize FFT size
		 * @param sampleRate Sample rate
		 * @return Bin index
		 */
		std::size_t getEndBin(std::size_t fftSize, std::size_t sampleRate) const
		{
			return freqToBin(m_endFreq, fftSize, sampleRate);
		}

		/**
		 * @brief Get start frequency
		 * @return Start frequency
		 */
		double getStartFreq() const
		{
			return m_startFreq;
		}

		/**
		 * @brief Get end frequency
		 * @return End frequency
		 */
		double getEndFreq() const
		{
			return m_endFreq;
		}
	};

	/**
	 * @brief Number of frequency bands used in analysis.
	 */
	static constexpr int DEFAULT_NUM_BANDS = 4;

	/**
	 * @brief Interpolation factor for delay calculations.
	 */
	static constexpr int DEFAULT_INTERP_FACTOR = 4;

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
	 * @brief Compare two SyncCheckConfig objects
	 * @param other The other SyncCheckConfig object to compare with
	 * @return True if the objects are equal, false otherwise
	 */
	bool operator == (const SyncCheckConfig &other) const;

	/**
	 * @brief Compare two SyncCheckConfig objects
	 * @param other The other SyncCheckConfig object to compare with
	 * @return True if the objects are not equal, false otherwise
	 */
	bool operator != (const SyncCheckConfig &other) const { return !(*this == other); }

	/**
	 * @brief Initialize bands with custom start frequency, frequency range, and band separation
	 * @param start_freq Starting frequency of the first band in Hz
	 * @param freq_range Total frequency range to cover for all bands in Hz
	 * @param band_sep Separation between consecutive bands in Hz
	 */
	void initBands(const double start_freq, const double freq_range, const double band_sep);

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

	/**
	 * @brief Get the dump enabled flag
	 * @return The dump enabled flag
	 */
	bool getCorrelationDumpEnabled() const { return m_correlationDumpEnabled; }

	/**
	 * @brief Set the dump enabled flag
	 * @param enabled The dump enabled flag
	 */
	void setCorrelationDumpEnabled(bool enabled) { m_correlationDumpEnabled = enabled; }

private:
	std::size_t m_fftSize;		///< FFT size for analysis
	std::size_t m_overlapSize;	///< Overlap size for analysis and test signal generation
	std::size_t m_sampleRate;	///< Sampling rate of the signal
	std::size_t m_numBands;		///< Number of frequency bands
	std::size_t m_interpFactor;	///< Interpolation factor for delay calculations
	std::vector<SyncCheckConfig::Band> m_bands;	///< Band definitions
	bool m_correlationDumpEnabled = false; // Disabled by default

	/**
	 * @brief Initialize default band configuration
	 */
	void initDefaultBands();
};