#include "STFTProcessor.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <cstdlib>
#include "Logger.h"

STFTProcessor::STFTProcessor(int fftSize, int hopSize, int windowSize)
	: m_fftSize(fftSize)
{
	if (m_fftSize <= 0 || hopSize <= 0) {
		DBG_ERROR("FFT size and hop size must be positive.");
		return;
	}
	if (hopSize * 2 != m_fftSize) {
		DBG_ERROR("This two‐Hann example requires hopSize = fftSize/2 for perfect reconstruction.");
		return;
	}
	if (windowSize <= 0) {
		windowSize = m_fftSize;
	}
	else {
		if (windowSize > m_fftSize) {
			DBG_ERROR("Window size cannot exceed FFT size.");
			return;
		}
	}

	m_windowGenerator = std::make_unique<STFTWindowGenerator>(windowSize, hopSize);
	m_fftWrapper = std::make_unique<KissFFTWrapper>(m_fftSize);

	// For streaming inverse processing we initialize the overlap buffer.
	// For the typical two-Hann case (windowSize == fftSize) the IFFT produces fftSize samples,
	// of which the first hopSize samples (i.e. fftSize/2) are output and the remaining fftSize/2 samples are overlapped.
	// In the more general case, we consider the first windowSize samples and overlap (windowSize - hopSize) samples.
	m_overlapBuffer.resize(m_windowGenerator->getWindowLength() - m_windowGenerator->getHopSize(), 0.0f);
}

STFTProcessor::~STFTProcessor() = default;

std::vector<std::vector<kiss_fft_cpx>> STFTProcessor::forward(const std::vector<float> &input)
{
	const int numFrames = (static_cast<int>(input.size()) - m_windowGenerator->getWindowLength() + m_windowGenerator->getHopSize()) / m_windowGenerator->getHopSize();
	if (numFrames <= 0) {
		return {};
	}

	std::vector<std::vector<kiss_fft_cpx>> frames;
	frames.reserve(numFrames);

	std::vector<float> fftIn(m_fftSize, 0.0f);
	std::vector<kiss_fft_cpx> fftOut(m_fftSize / 2 + 1);

	for (int f = 0; f < numFrames; f++) {
		const int start = f * m_windowGenerator->getHopSize();
		std::fill(fftIn.begin(), fftIn.end(), 0.0f);

		// Apply window
		auto window = m_windowGenerator->getAnalysisWindow();
		for (int n = 0; n < m_windowGenerator->getWindowLength(); n++) {
			int idx = start + n;
			if (idx < static_cast<int>(input.size())) {
				fftIn[n] = input[idx] * window[n];
			}
		}

		// Real-to-complex FFT
		m_fftWrapper->forward(fftIn, fftOut);
		frames.push_back(fftOut);
	}

	return frames;
}

// Process a single frame (assumed to be produced by forward()).
// This method performs the IFFT, scales, applies the synthesis window,
// then overlap-adds the stored tail (if any) and returns hopSize new samples.
// The new overlap (tail) is stored for the next call.
std::vector<float> STFTProcessor::processFrame(const std::vector<kiss_fft_cpx> &frame)
{
	if (frame.size() != static_cast<size_t>(m_fftSize / 2 + 1)) {
		throw std::runtime_error("Frame size mismatch in processFrame.");
	}

	std::vector<float> ifftOut(m_fftSize, 0.0f);

	// Perform inverse FFT
	m_fftWrapper->inverse(frame, ifftOut, true);

	// Apply synthesis window (only for first windowSize_ samples)
	auto window = m_windowGenerator->getSynthesisWindow();
	for (int i = 0; i < m_windowGenerator->getWindowLength(); i++) {
		ifftOut[i] *= window[i];
	}

	// Compute output:
	// We output the first hopSize_ samples combined with the stored overlap.
	// (For the typical two-Hann, windowSize_ == fftSize_ and hopSize_ == fftSize_/2.)
	std::vector<float> output(m_windowGenerator->getHopSize(), 0.0f);
	for (int i = 0; i < m_windowGenerator->getHopSize(); i++) {
		// Add the stored overlap (if any) to the new samples.
		// Note: m_overlapBuffer is sized (windowSize_ - hopSize_).
		float oldOverlap = (i < static_cast<int>(m_overlapBuffer.size())) ? m_overlapBuffer[i] : 0.0f;
		output[i] = ifftOut[i] + oldOverlap;
	}

	// Update the overlap buffer with the tail of the current IFFT output.
	// That is, the samples from index hopSize_ up to windowSize_ (which are meant to be overlapped with the next frame).
	for (int i = 0; i < m_windowGenerator->getWindowLength() - m_windowGenerator->getHopSize(); i++) {
		m_overlapBuffer[i] = ifftOut[m_windowGenerator->getHopSize() + i];
	}

	return output;
}

// Batch inverse: process multiple frames sequentially and concatenate the output.
// This method calls processFrame() for each frame, so that internal overlap is maintained across calls.
std::vector<float> STFTProcessor::inverse(const std::vector<std::vector<kiss_fft_cpx>> &frames)
{
	std::vector<float> output;
	// Process each frame sequentially.
	for (const auto &frame : frames) {
		std::vector<float> seg = processFrame(frame);
		output.insert(output.end(), seg.begin(), seg.end());
	}
	return output;
}
