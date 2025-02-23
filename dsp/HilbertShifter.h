#pragma once

#include "STFTProcessor.h"
#include <vector>
#include <memory>
#include <cmath>
#include <functional>
/**
 * @brief Class to perform Hilbert transform and frequency shifting using STFT
 */
class HilbertShifter
{
public:
	/**
	 * @brief Constructor
	 * @param blockSize FFT block size
	 * @param hopSize Hop size (must be blockSize/2 or blockSize/4)
	 * @param sampleRate Sample rate in Hz
	 */
	HilbertShifter(int blockSize, int hopSize, float sampleRate);

	/**
	 * @brief Prepare analytic signal in frequency domain. (FFT of Hilbert transform)
	 * @param input Input buffer of blockSize samples (real data)
	 * @param output Output complex analytical frequency domain signal buffer of blockSize samples
	 */
	void forward(const std::vector<float> &input, std::vector<kiss_fft_cpx> &output);

	/**
	 * @brief Inverse transform from frequency domain to time domain.
	 * @param input Input complex analytical frequency domain buffer of blockSize samples
	 * @param output Output complex time domain signal buffer of hopSize samples
	 */
	void inverse(const std::vector<kiss_fft_cpx> &input, std::vector<kiss_fft_cpx> &output);

	/**
	 * @brief Perform Hilbert transform on input signal
	 * @param input Input buffer of blockSize samples
	 * @param output Output complex time domain signal buffer of hopSize samples
	 * @param modifySpectrumCb Optional callback to modify the Hilbert spectrum before transform to time domain
	 */
	void transform(const std::vector<float> &input, std::vector<kiss_fft_cpx> &output, std::function<void(std::vector<kiss_fft_cpx>&)> modifySpectrumCb = [](std::vector<kiss_fft_cpx>&) {});

	/**
	 * @brief Perform frequency shift on input complex analytic time domain signal
	 * @param input Input complex analytic time domain signal buffer of hopSize samples
	 * @param output Output buffer of hopSize samples
	 * @param freqOffset Frequency shift in Hz (positive shifts up, negative shifts down)
	 */
	void shift(const std::vector<kiss_fft_cpx> &input, std::vector<float> &output, float freqOffset);

	/**
	 * @brief Process a block of samples with frequency shifting
	 * @param input Input buffer of blockSize samples
	 * @param output Output buffer of hopSize samples
	 * @param freqOffset Frequency shift in Hz (positive shifts up, negative shifts down)
	 */
	void transformWithShift(const std::vector<float> &input, std::vector<float> &output, float freqOffset);

	/**
	 * @brief Get the required block size for input
	 * @return Block size in samples
	 */
	size_t getBlockSize() const { return m_stftProc->getSize(); }

	/**
	 * @brief Get the output hop size
	 * @return Hop size in samples
	 */
	size_t getHopSize() const { return m_hopSize; }

private:
	/**
	 * @brief Convert half-spectrum to full analytic spectrum
	 * @param half Half spectrum (length M = fftSize/2+1)
	 * @param full Output full analytic spectrum (length fftSize)
	 */
	void halfToFullAnalytic(const std::vector<kiss_fft_cpx> &half, std::vector<kiss_fft_cpx> &full);

	/**
	 * @brief Compute analytic half-spectrum from regular half-spectrum
	 * @param frame Input half-spectrum
	 * @param analytic Output analytic half-spectrum
	 */
	void computeAnalyticHalfSpectrum(const std::vector<kiss_fft_cpx> &frame, std::vector<kiss_fft_cpx> &analytic);

private:
	std::unique_ptr<STFTProcessor> m_stftProc;
	int m_hopSize;
	float m_sampleRate;
	double m_phase;

	// Working buffers
	std::vector<kiss_fft_cpx> m_analyticHalf;
	std::vector<kiss_fft_cpx> m_fullAnalytic;
	std::vector<kiss_fft_cpx> m_hilbertOutput;
};
