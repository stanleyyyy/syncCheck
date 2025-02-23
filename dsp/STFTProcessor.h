#pragma once
#include <vector>
#include <memory>
#include "kiss_fftr.h"
#include "STFTWindowGenerator.h"
#include "KissFFTWrapper.h"
#include "KissFFTComplexWrapper.h"

/**
 * Class to perform a streaming STFT (short-time Fourier transform).
 * It is used to convert a time-domain signal into a frequency-domain signal and back.
 * It uses a Hann window to reduce spectral leakage between consecutive frames.
 */
class STFTProcessor
{
public:
	/**
	 * @param fftSize    Size of the FFT (e.g. 1024).
	 * @param hopSize    Must be exactly fftSize/2 or fftSize/4 for perfect reconstruction.
	 * @param windowSize Typically the same as fftSize (for a full Hann).
	 */
	STFTProcessor(int fftSize, int hopSize, int windowSize = -1);
	~STFTProcessor();

	/**
	 * Forward STFT:
	 *   1) Splits input into frames of windowSize, spaced by hopSize
	 *   2) Applies Hann window
	 *   3) Computes real-to-complex FFT
	 */
	std::vector<std::vector<kiss_fft_cpx>> forward(const std::vector<float> &input);

	/**
	 * Inverse STFT (streaming version):
	 * Processes a sequence of frames, performing overlap–add with an internal buffer.
	 * Returns hopSize samples per frame processed using a real IFFT.
	 */
	void inverse(const std::vector<std::vector<kiss_fft_cpx>> &frames, std::vector<float> &output);

	/**
	 * Inverse STFT using complex IFFT for full complex frames.
	 * Processes a sequence of full complex frames and concatenates the output.
	 */
	void complexInverse(const std::vector<std::vector<kiss_fft_cpx>> &frames, std::vector<kiss_fft_cpx> &output);

	/**
	 * Process a single frame and return hopSize samples.
	 * This method maintains internal state for overlap–add.
	 */
	void processFrame(const std::vector<kiss_fft_cpx> &frame, std::vector<float> &output);

	/**
	 * @brief Process a frame and perform complex overlap-add synthesis
	 * @param frame Input complex frame
	 * @param output Output complex samples (hopSize complex samples)
	 */
	void complexProcessFrame(const std::vector<kiss_fft_cpx> &frame, std::vector<kiss_fft_cpx> &output);

	/**
	 * Get the FFT size.
	 * @return FFT size
	 */
	size_t getSize() const { return m_fftSize; }

private:
	int m_fftSize;
	std::unique_ptr<STFTWindowGenerator> m_windowGenerator;
	std::unique_ptr<KissFFTWrapper> m_fftWrapper;
	std::unique_ptr<KissFFTComplexWrapper> m_complexFFTWrapper;
	std::vector<float> m_overlapBuffer;
	std::vector<float> m_ifftBuffer;
	std::vector<float> m_timeFrameBuffer;
	std::vector<float> m_recenteredBuffer;

	// Complex overlap buffer for complex synthesis
	std::vector<kiss_fft_cpx> m_complexOverlapBuffer;
};
