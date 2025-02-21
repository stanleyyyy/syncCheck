#include "STFTWindowGenerator.h"
#include "sndfile.h"
#include "Logger.h"
#include <cmath>

std::vector<float> testWindowedOLA(const std::vector<float> &input, const STFTWindowGenerator &windowGen)
{
	// Extract window/hop info
	int wLen = windowGen.getWindowLength();
	int hop = windowGen.getHopSize();
	const auto &analysisWin = windowGen.getAnalysisWindow();
	const auto &synthesisWin = windowGen.getSynthesisWindow();

	// Compute how many full frames we can process
	int numFrames = (static_cast<int>(input.size()) - wLen + hop) / hop;
	if (numFrames < 1) {
		return {}; // Not enough samples for even one frame
	}

	// We'll store overlap-add output in a buffer large enough
	// to accommodate all frames. Add a bit of padding at the end.
	std::vector<float> output(input.size() + wLen, 0.0f);

	// For each frame, multiply by analysis window, then by synthesis window,
	// and overlap-add into 'output'.
	for (int f = 0; f < numFrames; f++) {
		int start = f * hop;

		// Build the "windowed" frame (analysis)
		std::vector<float> frame(wLen, 0.0f);
		for (int n = 0; n < wLen; n++) {
			int idx = start + n;
			if (idx >= 0 && idx < static_cast<int>(input.size())) {
				frame[n] = input[idx] * static_cast<float>(analysisWin[n]);
			}
		}

		// Now apply the synthesis window and overlap-add
		for (int n = 0; n < wLen; n++) {
			output[start + n] += frame[n] * static_cast<float>(synthesisWin[n]);
		}
	}

	return output;
}

void testOverlapCase(int overlapPercent, const char* outputFilename)
{
	const int sampleRate = 44100;
	const float frequency = 440.0f;  // A4 note
	const int duration = 1;  // seconds
	const int numSamples = sampleRate * duration;
	const int blockSize = 8192;
	const int hopSize = blockSize - (blockSize * overlapPercent / 100);

	// Generate sine wave
	std::vector<float> signal(numSamples);
	for (int i = 0; i < numSamples; ++i) {
		signal[i] = std::sin(2.0f * M_PI * frequency * i / sampleRate);
	}

	STFTWindowGenerator windowGen(blockSize, blockSize - hopSize);
	std::vector<float> olaOutput = testWindowedOLA(signal, windowGen);

	// Save reconstructed signal to WAV file
	SF_INFO sfinfo;
	sfinfo.samplerate = sampleRate;
	sfinfo.channels = 1;
	sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

	SNDFILE* outfile = sf_open(outputFilename, SFM_WRITE, &sfinfo);
	if (outfile) {
		sf_writef_float(outfile, olaOutput.data(), olaOutput.size());
		sf_close(outfile);
	} else {
		DBG_ERROR("Failed to open output WAV file: " << outputFilename);
	}

	// Calculate reconstruction error
	float maxError = 0.0f;
	size_t validSamples = std::min(signal.size(), olaOutput.size());
	for (size_t i = 0; i < validSamples; ++i) {
		float error = std::abs(signal[i] - olaOutput[i]);
		maxError = std::max(maxError, error);
	}

	DBG_INFO("Overlap " << overlapPercent << "% test results:");
	DBG_INFO("Max reconstruction error: " << maxError);
	DBG_INFO("Output saved to: " << outputFilename);
}

int main()
{
	// Test both 50% and 75% overlap cases
	testOverlapCase(50, "reconstructed_50pct.wav");
	testOverlapCase(75, "reconstructed_75pct.wav");
	return 0;
}
