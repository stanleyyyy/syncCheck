#pragma once
#include <vector>
#include <memory>
#include "kiss_fftr.h"
#include "STFTWindowGenerator.h"
#include "KissFFTWrapper.h"

/**
 * Class to perform a streaming STFT (short-time Fourier transform).
 * This is a streaming version of the FFT.
 * It is used to convert a time-domain signal into a frequency-domain signal and back.
 * It uses a Hann window to reduce spectral leakage between consecutive frames.
 */
class STFTProcessor
{
public:
	/**
	 * @param fftSize    Size of the FFT (e.g. 1024).
	 * @param hopSize    Must be exactly fftSize/2 for the two‐Hann method here.
	 * @param windowSize Typically the same as fftSize (for a full Hann).
	 */
	STFTProcessor(int fftSize, int hopSize, int windowSize = -1);
	~STFTProcessor();

	/**
	 * Forward STFT:
	 *   1) Splits input into frames of windowSize, spaced by hopSize
	 *   2) Applies Hann window
	 *   3) Computes real‐to‐complex FFT
	 */
	std::vector<std::vector<kiss_fft_cpx>> forward(const std::vector<float> &input);

	/**
	 * Inverse STFT (streaming version):
	 * Processes a sequence of frames, performing overlap–add with an internal buffer.
	 * Returns hopSize samples per frame processed.
	 */
	void inverse(const std::vector<std::vector<kiss_fft_cpx>>& frames, std::vector<float>& output);

	/**
	 * Process a single frame and return hopSize samples.
	 * This method maintains internal state for overlap–add.
	 */
	std::vector<float> processFrame(const std::vector<kiss_fft_cpx> &frame);

private:
	int m_fftSize;
	std::unique_ptr<STFTWindowGenerator> m_windowGenerator;
	std::unique_ptr<KissFFTWrapper> m_fftWrapper;
	std::vector<float> m_overlapBuffer;
	std::vector<float> m_ifftBuffer;
	std::vector<float> m_timeFrameBuffer;
	std::vector<float> m_recenteredBuffer;
};
