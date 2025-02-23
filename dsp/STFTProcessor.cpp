#include "STFTProcessor.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <cstdlib>
#include <cassert>
#include "Logger.h"
#include "kiss_fft.h"

STFTProcessor::STFTProcessor(int fftSize, int hopSize, int windowSize)
	: m_fftSize(fftSize)
{
	ASSERT_WITH_ERROR(fftSize > 0 && hopSize > 0, "FFT size and hop size must be positive.");
	ASSERT_WITH_ERROR(hopSize == m_fftSize / 2 || hopSize == m_fftSize / 4, "STFTProcessor requires hopSize = fftSize/2 or fftSize/4 for perfect reconstruction.");

	if (windowSize <= 0) {
		windowSize = fftSize; // typical
	} else {
		ASSERT_WITH_ERROR(windowSize <= fftSize, "Window size cannot exceed FFT size.");
	}

	// overlap = windowSize - hopSize
	m_windowGenerator = std::make_unique<STFTWindowGenerator>(windowSize, (windowSize - hopSize));
	m_fftWrapper = std::make_unique<KissFFTWrapper>(m_fftSize);
	m_complexFFTWrapper = std::make_unique<KissFFTComplexWrapper>(m_fftSize);

	// Initialize member buffers
	m_ifftBuffer.resize(m_fftSize, 0.0f);
	m_timeFrameBuffer.resize(m_windowGenerator->getWindowLength(), 0.0f);
	m_recenteredBuffer.resize(m_windowGenerator->getWindowLength(), 0.0f);
	m_overlapBuffer.resize(m_windowGenerator->getWindowLength() - m_windowGenerator->getHopSize(), 0.0f);
	m_complexOverlapBuffer.resize(m_windowGenerator->getWindowLength(), {0.0f, 0.0f});
}

STFTProcessor::~STFTProcessor() = default;

std::vector<std::vector<kiss_fft_cpx>> STFTProcessor::forward(const std::vector<float> &input)
{
	const int wLen = m_windowGenerator->getWindowLength();
	const int hop = m_windowGenerator->getHopSize();

	// Number of complete frames that fit
	int numFrames = (static_cast<int>(input.size()) - wLen + hop) / hop;
	if (numFrames <= 0) {
		return {};
	}

	std::vector<std::vector<kiss_fft_cpx>> frames;
	frames.reserve(numFrames);

	std::vector<float> fftIn(m_fftSize, 0.0f);
	std::vector<kiss_fft_cpx> fftOut(m_fftSize / 2 + 1);

	const auto &analysisWin = m_windowGenerator->getAnalysisWindow();

	for (int f = 0; f < numFrames; f++) {
		const int start = f * hop;
		std::fill(fftIn.begin(), fftIn.end(), 0.0f);

		// Apply analysis window
		for (int n = 0; n < wLen; n++) {
			int idx = start + n;
			if (idx < static_cast<int>(input.size())) {
				fftIn[n] = input[idx] * static_cast<float>(analysisWin[n]);
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
void STFTProcessor::processFrame(const std::vector<kiss_fft_cpx> &frame, std::vector<float> &output)
{
	ASSERT_WITH_ERROR(frame.size() == static_cast<size_t>(m_fftSize / 2 + 1), "Frame size mismatch in processFrame.");

	std::fill(m_ifftBuffer.begin(), m_ifftBuffer.end(), 0.0f);
	// Inverse FFT with built-in scaling (Kiss FFT already applies 1/fftSize)
	m_fftWrapper->inverse(frame, m_ifftBuffer, true);

	const int wLen = m_windowGenerator->getWindowLength();
	const int hop = m_windowGenerator->getHopSize();

	// Use only the first wLen samples from the IFFT output.
	for (int i = 0; i < wLen; i++) {
		m_timeFrameBuffer[i] = m_ifftBuffer[i];
	}

	// Apply recentering only if hop is not half the window length (i.e. for 75% overlap)
	if (hop != wLen / 2) {
		int shift = wLen / 2; // effective delay compensation
		for (int i = 0; i < wLen; i++) {
			int src = (i + shift) % wLen;
			m_recenteredBuffer[i] = m_timeFrameBuffer[src];
		}
		std::copy(m_recenteredBuffer.begin(), m_recenteredBuffer.end(), m_timeFrameBuffer.begin());
	}

	// Apply synthesis window
	const auto& synWin = m_windowGenerator->getSynthesisWindow();
	for (int i = 0; i < wLen; i++) {
		m_timeFrameBuffer[i] *= static_cast<float>(synWin[i]);
	}

	// Overlap-add: output first hop samples + stored overlap
	output.resize(hop, 0.0f);
	for (int i = 0; i < hop; i++) {
		float oldOverlap = (i < static_cast<int>(m_overlapBuffer.size())) ? m_overlapBuffer[i] : 0.0f;
		output[i] = m_timeFrameBuffer[i] + oldOverlap;
	}

	// Update overlap buffer with the tail of the current frame.
	for (int i = 0; i < wLen - hop; i++) {
		m_overlapBuffer[i] = m_timeFrameBuffer[hop + i];
	}
}

void STFTProcessor::complexProcessFrame(const std::vector<kiss_fft_cpx> &frame, std::vector<kiss_fft_cpx> &output)
{
	ASSERT_WITH_ERROR(frame.size() == static_cast<size_t>(m_fftSize), "Frame size mismatch in complexProcessFrame.");

	std::vector<kiss_fft_cpx> timeDomain;
	// Use KissFFTComplexWrapper to perform complex inverse FFT.
	m_complexFFTWrapper->inverse(frame, timeDomain, true);

	const int wLen = m_windowGenerator->getWindowLength();
	const int hop = m_windowGenerator->getHopSize();

	// Apply synthesis window
	const auto& synWin = m_windowGenerator->getSynthesisWindow();
	for (int i = 0; i < wLen; i++) {
		float windowVal = static_cast<float>(synWin[i]);
		timeDomain[i].r *= windowVal;
		timeDomain[i].i *= windowVal;
	}

	// Overlap-add: output first hop samples + stored overlap
	output.resize(hop);
	for (int i = 0; i < hop; i++) {
		float oldOverlapR = (i < static_cast<int>(m_complexOverlapBuffer.size())) ? m_complexOverlapBuffer[i].r : 0.0f;
		float oldOverlapI = (i < static_cast<int>(m_complexOverlapBuffer.size())) ? m_complexOverlapBuffer[i].i : 0.0f;
		output[i].r = timeDomain[i].r + oldOverlapR;
		output[i].i = timeDomain[i].i + oldOverlapI;
	}

	// Update overlap buffer with the tail of the current frame
	for (int i = 0; i < wLen - hop; i++) {
		m_complexOverlapBuffer[i] = timeDomain[hop + i];
	}
}

void STFTProcessor::inverse(const std::vector<std::vector<kiss_fft_cpx>> &frames, std::vector<float> &output)
{
	const size_t hopSize = m_windowGenerator->getHopSize();
	output.resize(frames.size() * hopSize);
	size_t outPos = 0;

	for (const auto &frame : frames) {
		std::vector<float> seg;
		processFrame(frame, seg);
		std::copy(seg.begin(), seg.end(), output.begin() + outPos);
		outPos += hopSize;
	}
}

void STFTProcessor::complexInverse(const std::vector<std::vector<kiss_fft_cpx>> &frames, std::vector<kiss_fft_cpx> &output)
{
	const size_t hopSize = m_windowGenerator->getHopSize();
	output.resize(frames.size() * hopSize);
	size_t outPos = 0;

	for (const auto &frame : frames) {
		// Here we assume each frame is a full complex spectrum of length m_fftSize.
		std::vector<kiss_fft_cpx> seg;
		complexProcessFrame(frame, seg);
		std::copy(seg.begin(), seg.end(), output.begin() + outPos);
		outPos += hopSize;
	}
}
