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
#include <memory>

#include "SyncCheckConfig.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Forward declare the FFT configuration type
struct kiss_fftr_state;
typedef struct kiss_fftr_state* kiss_fftr_cfg;

// Define the deleter type
using kiss_fftr_deleter = void(*)(void*);

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
	 * @brief Apply window and perform forward FFT on input data
	 * @param input Reference to input buffer of size m_blockSize
	 * @return Reference to the internal frequency domain data
	 */
	void applyWindowAndFFT(const std::vector<float>& input, std::vector<kiss_fft_cpx>& freqData);

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
	 * @param input_background Reference to background input buffer
	 * @param input_to_shift Reference to input buffer to shift
	 * @param output Reference to output buffer
	 * @param clearBand Band to clear
	 */
	void processBlockWithShift(const std::vector<float>& input_background, const std::vector<float>& input_to_shift,
							 std::vector<float>& output, const SyncCheckConfig::Band &clearBand);

	/**
	 * @brief Process a block with frequency shifting using pre-computed FFT data
	 * @param fft_data_background Pre-computed background FFT data
	 * @param fft_data_to_shift Pre-computed FFT data to shift
	 * @param output Reference to output buffer
	 * @param clearBand Band to clear
	 */
	void processBlockWithShiftFFT(const std::vector<kiss_fft_cpx>& fft_data_background,
								const std::vector<kiss_fft_cpx>& fft_data_to_shift,
								std::vector<float>& output,
								const SyncCheckConfig::Band &clearBand);

	/**
	 * @brief Get the band isolation metric in dB.
	 *
	 * @return Band isolation in dB.
	 */
	float getBandIsolation() const;

private:
	/**
	 * @brief Configuration for the band processor.
	 */
	SyncCheckConfig m_config;

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
	std::vector<kiss_fft_cpx> m_freqData_background;
	std::vector<kiss_fft_cpx> m_freqData_to_shift;

	/**
	 * @brief Working buffer for band-filtered data.
	 */
	std::vector<kiss_fft_cpx> m_workingFreqData;

	/**
	 * @brief Reference to the associated frequency band.
	 */
	SyncCheckConfig::Band m_band;

	/**
	 * @brief Buffer for overlap‐add.
	 */
	std::vector<float> m_overlapBuffer;

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

	float m_shiftFreq;           // Frequency shift amount in Hz
	float m_phaseAccumulator;    // Accumulated phase for continuous modulation
	std::vector<float> m_modulatorBuffer;  // Buffer for modulation waveform
};

