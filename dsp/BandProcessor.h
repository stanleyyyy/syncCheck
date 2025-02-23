#pragma once

#include "STFTProcessor.h"
#include "HilbertShifter.h"
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
	 * @brief Forward STFT via built-in STFT processor.
	 * @param input Input time-domain data
	 * @param output Output frequency-domain data
	 */
	void forward(const std::vector<float>& input, std::vector<kiss_fft_cpx>& output);

	/**
	 * @brief Inverse STFT via built-in STFT processor.
	 * @param frames Input frames.
	 * @param output Output buffer.
	 */
	void inverse(const std::vector<std::vector<kiss_fft_cpx>>& frames, std::vector<float>& output);

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
	 * @param background Reference to background input buffer
	 * @param reference Reference to input buffer to shift
	 * @param output Reference to output buffer
	 * @param clearBand Band to clear
	 */
	void frequencyShift(const std::vector<float>& background, const std::vector<float>& reference,
							 std::vector<float>& output, const SyncCheckConfig::Band &clearBand);

	/**
	 * @brief Process a block of samples with frequency shifting
	 * @param analyticBackground Reference to background input buffer
	 * @param analyticReference Reference to input buffer to shift
	 * @param output Reference to output buffer
	 */
	void frequencyShift(const std::vector<kiss_fft_cpx> &analyticBackground, const std::vector<kiss_fft_cpx> &analyticReference, std::vector<float> &output);

	/**
	 * @brief Prepare analytic signals for frequency shifting.
	 * @param background Background input signal.
	 * @param reference Signal to be shifted.
	 * @param analyticBackground Output buffer for background signal.
	 * @param analyticReference Output buffer for signal to shift.
	 * @param clearBand Clear band.
	 */
	void prepareAnalyticSignals(const std::vector<float>& background, const std::vector<float>& reference,
								std::vector<kiss_fft_cpx>& analyticBackground, std::vector<kiss_fft_cpx>& analyticReference,
								const SyncCheckConfig::Band &clearBand);

	/**
	 * @brief Get the band isolation metric in dB.
	 *
	 * @return Band isolation in dB.
	 */
	float getBandIsolation() const;

private:
	/**
	 * @brief Compute a cosine taper window.
	 *
	 * @param length Number of FFT bins in the band.
	 * @param taperFraction Fraction of bins at each edge to taper.
	 * @param isClearBand True if the band is the clear band, false otherwise.
	 * @return Precomputed taper window.
	 */
	static std::vector<float> computeTaperWindow(std::size_t length, float taperFraction, const bool &isClearBand = false);

	/**
	 * @brief Compute the taper fraction for a given desired transition width and band width.
	 * @param desiredTransitionWidth Desired transition width in Hz.
	 * @param bandWidthHz Width of the band in Hz.
	 * @return Taper fraction.
	 */
	static float computeTaperFraction(float desiredTransitionWidth, float bandWidthHz);

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
	 * @brief Hilbert shifter for frequency shifting.
	 */
	std::unique_ptr<HilbertShifter> m_hilbertShifterBackground;

	/**
	 * @brief Hilbert shifter for frequency shifting.
	 */
	std::unique_ptr<HilbertShifter> m_hilbertShifterReference;

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

	/**
	 * @brief Precomputed taper window for the shifting band.
	 */
	std::vector<float> m_referenceTaper;

	/**
	 * @brief Precomputed taper window for the clear band.
	 */
	std::vector<float> m_backgroundTaper;

	/**
	 * @brief Working buffer for shifted full spectrum.
	 */
	std::vector<kiss_fft_cpx> m_analyticBackground;

	/**
	 * @brief Working buffer for shifted full spectrum.
	 */
	std::vector<kiss_fft_cpx> m_analyticReference;
};
