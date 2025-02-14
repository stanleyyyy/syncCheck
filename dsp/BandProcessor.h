#pragma once

#include "STFTProcessor.h"
#include <vector>
#include <complex>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <random>
#include <stdexcept>
#include <cmath>
#include <memory>

#include "SyncCheckConfig.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief Class for processing a specific frequency band using the overlap‐add method.
 */
class BandProcessor
{
public:
	/**
	 * @brief Constructs a BandProcessor for a given frequency band.
	 *
	 * @param config Configuration for the band processor.
	 * @param band Reference to the frequency band to process.
	 */
	BandProcessor(const SyncCheckConfig &config, const SyncCheckConfig::Band &band);

	/**
	 * @brief Copy constructor.
	 */
	BandProcessor(const BandProcessor &other);

	/**
	 * @brief Move constructor.
	 */
	BandProcessor(BandProcessor &&other) noexcept;

	/**
	 * @brief Destructor.
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
	 * @brief Process a block of FFT data and produce an output block.
	 *
	 * @param fft_data Reference to FFT data.
	 * @param output Reference to output samples.
	 */
	void processBlockFFT(const std::vector<kiss_fft_cpx> &fft_data, std::vector<float>& output);

	/**
	 * @brief Process a block of samples with frequency shifting
	 * @param input_background Reference to background input buffer
	 * @param input_to_shift Reference to input buffer to shift
	 * @param output Reference to output buffer
	 * @param clearBand Band to clear
	 */
	void processBlockWithShift(const std::vector<float>& input_background, const std::vector<float>& input_to_shift,
							 std::vector<float>& output, const SyncCheckConfig::Band &clearBand);

	/**
	 * @brief Get the band isolation metric in dB.
	 *
	 * @return Band isolation in dB.
	 */
	float getBandIsolation() const;

	/**
	 * @brief Apply window and perform FFT on input data
	 * @param input Input time-domain data
	 * @param output Output frequency-domain data
	 */
	void applyWindowAndFFT(const std::vector<float>& input, std::vector<kiss_fft_cpx>& output);

	/**
	 * @brief Process a block with frequency shifting using pre-computed FFT data
	 * @param fft_data_background Background FFT data
	 * @param fft_data_to_shift FFT data to shift
	 * @param output Output buffer
	 * @param clearBand Band to clear from background
	 */
	void processBlockWithShiftFFT(const std::vector<kiss_fft_cpx>& fft_data_background,
								const std::vector<kiss_fft_cpx>& fft_data_to_shift,
								std::vector<float>& output,
								const SyncCheckConfig::Band& clearBand);

	/**
	 * @brief Inverse STFT via built-in STFT processor.
	 * @param frames Input frames.
	 * @param output Output buffer.
	 */
	void inverse(const std::vector<std::vector<kiss_fft_cpx>>& frames, std::vector<float>& output);

private:
	/**
	 * @brief Configuration for the band processor.
	 */
	SyncCheckConfig m_config;

	/**
	 * @brief STFT processor for signal analysis and synthesis.
	 */
	std::unique_ptr<STFTProcessor> m_stftProcessor;

	/**
	 * @brief Reference to the associated frequency band.
	 */
	SyncCheckConfig::Band m_band;

	/**
	 * @brief Working buffer for frequency-domain data.
	 */
	std::vector<kiss_fft_cpx> m_workingFreqData;

	/**
	 * @brief Accumulated power within the band.
	 */
	double m_inBandPower;

	/**
	 * @brief Accumulated power outside the band.
	 */
	double m_outBandPower;
};

