#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include "CLI11.hpp"
#include "Logger.h"
#include "HilbertShifter.h"
#include "WavFile.h"

using namespace std;

/**
 * @brief Application configuration parameters.
 */
struct AppConfig
{
	/**
	 * @brief Input WAV file name
	 */
	std::string m_inputFile;

	/**
	 * @brief Start frequency of processing bands in Hz.
	 */
	float m_startFreq = 0.0f;

	/**
	 * @brief End frequency of processing bands in Hz.
	 */
	float m_stopFreq = 2000.0f;

	/**
	 * @brief Target start frequency for shifting in Hz.
	 */
	float m_targetStart = 12000.0f;

	/**
	 * @brief FFT block size.
	 */
	unsigned int m_blockSize = 8192;

	/**
	 * @brief Overlap percent.
	 */
	unsigned int m_overlapPercent = 75;
};

bool parseCommandLine(int argc, char *argv[], AppConfig &config, int &exitCode)
{
	CLI::App app { "Frequency shifts a WAV file with HilbertShifter and shifts it back." };
	app.option_defaults()->always_capture_default();

	app.add_option("input", config.m_inputFile, "Input WAV file")->required();
	app.add_option("--block-size", config.m_blockSize, "FFT block size")->check(CLI::PositiveNumber);
	app.add_option("--overlap", config.m_overlapPercent, "Overlap percent")->check(CLI::IsMember({ 50, 75 }));
	app.add_option("--start-freq", config.m_startFreq, "Start frequency in Hz");
	app.add_option("--stop-freq", config.m_stopFreq, "Stop frequency in Hz");
	app.add_option("--target-freq", config.m_targetStart, "Target start frequency in Hz");

	try {
		app.parse(argc, argv);
	} catch (const CLI::ParseError &e) {
		exitCode = app.exit(e);
		return false;
	}
	return true;
}

/**
 * @brief Load WAV file.
 * If the file has multiple channels, this function converts it to mono by taking the first channel.
 */
bool loadWav(const string &filename, int &sampleRate, vector<float> &data)
{
	WavReader infile;
	if (!infile.open(filename)) {
		DBG_ERROR("Error opening file: " << filename);
		return false;
	}
	sampleRate = infile.getSampleRate();
	const unsigned int channels = infile.getChannels();
	// Read all channels
	vector<float> temp(infile.getLengthInFrames() * channels);
	const size_t frames = infile.readFrames(temp.data(), infile.getLengthInFrames());
	// Convert to mono by taking the first channel.
	data.resize(frames);
	for (size_t i = 0; i < frames; i++) {
		data[i] = temp[i * channels] / 4;
	}
	return true;
}

/**
 * @brief Save WAV file.
 */
bool saveWav(const string &filename, int sampleRate, const vector<float> &data, int channels)
{
	WavWriter outfile;
	if (!outfile.open(filename, channels, sampleRate)) {
		DBG_ERROR("Error creating file: " << filename);
		return false;
	}
	outfile.writeFrames(data.data(), data.size() / channels);
	return true;
}

/**
 * @brief Main test routine performing per-buffer (hop-by-hop) processing.
 *
 * Instead of computing the STFT on the entire file at once,
 * this implementation accumulates samples into a processing buffer.
 * When the buffer is full (i.e. one analysis window is collected),
 * it processes a single frame: computing the FFT, constructing the analytic spectrum,
 * performing frequency shifting and its inverse, and finally reconstructing hop-sized segments.
 */
int main(int argc, char *argv[])
{
	AppConfig config;
	int exitCode = 0;
	if (!parseCommandLine(argc, argv, config, exitCode)) {
		return exitCode;
	}

	int sampleRate;
	vector<float> inputSignal;
	if (!loadWav(config.m_inputFile, sampleRate, inputSignal)) {
		DBG_ERROR("Failed to load WAV file.");
		return 1;
	}

	// Set STFT parameters using config values
	const size_t fftSize = config.m_blockSize;
	const size_t overlap = config.m_overlapPercent;
	const size_t hopSize = fftSize - fftSize * overlap / 100;

	// Determine processing block size from the analysis window
	size_t blockSize = fftSize;
	vector<float> processingBuffer(blockSize, 0.0f);
	vector<float> processingBufferRestore(blockSize, 0.0f);

	size_t filledSamples = 0;
	size_t filledSamplesRestore = 0;
	size_t inputPos = 0;
	vector<float> shiftedSignal;
	vector<float> restoredSignal;

	vector<float> shiftedBlock;
	vector<float> restoredBlock;

	HilbertShifter hilbertShifter(fftSize, hopSize, sampleRate);
	HilbertShifter hilbertShifterRestore(fftSize, hopSize, sampleRate);

	// Process input data hop-by-hop (per processing block)
	while (inputPos < inputSignal.size()) {
		// Calculate how many samples we can copy to the processing buffer.
		size_t spaceInBuffer = blockSize - filledSamples;
		size_t samplesToCopy = min(inputSignal.size() - inputPos, spaceInBuffer);

		// Copy samples into processing buffer.
		for (size_t i = 0; i < samplesToCopy; i++) {
			processingBuffer[filledSamples + i] = inputSignal[inputPos + i];
		}
		filledSamples += samplesToCopy;
		inputPos += samplesToCopy;

		// When we have a full block, process one frame.
		if (filledSamples >= (size_t)blockSize) {
			hilbertShifter.transformWithShift(processingBuffer, shiftedBlock, config.m_targetStart);

			// Append shifted samples to output
			shiftedSignal.insert(shiftedSignal.end(), shiftedBlock.begin(), shiftedBlock.begin() + hopSize);

			// Shift the processing buffer left by hopSize and update filled sample count.
			if (filledSamples > (size_t)hopSize) {
				std::copy(processingBuffer.begin() + hopSize, processingBuffer.begin() + filledSamples, processingBuffer.begin());
			}
			filledSamples -= hopSize;

			// Copy shifted samples to restore buffer
			for (size_t i = 0; i < hopSize; i++) {
				processingBufferRestore[filledSamplesRestore + i] = shiftedBlock[i] * 0.5f;
			}
			filledSamplesRestore += hopSize;

			// Process restore buffer only when we have a full block
			if (filledSamplesRestore >= (size_t)blockSize) {
				hilbertShifterRestore.transformWithShift(processingBufferRestore, restoredBlock, -config.m_targetStart);

				// Append processed samples to output
				restoredSignal.insert(restoredSignal.end(), restoredBlock.begin(), restoredBlock.begin() + hopSize);

				// Shift the restore buffer left by hopSize
				if (filledSamplesRestore > (size_t)hopSize) {
					std::copy(processingBufferRestore.begin() + hopSize,
							 processingBufferRestore.begin() + filledSamplesRestore,
							 processingBufferRestore.begin());
				}
				filledSamplesRestore -= hopSize;
			}
		}
	}

	// (Optional) Adjust overall scaling if needed.
	for (auto &sample : shiftedSignal) {
		sample *= 0.5f;
	}
	for (auto &sample : restoredSignal) {
		sample *= 0.5f;
	}

	string shiftedFilename = "hilbert_shifted_" + config.m_inputFile;
	string restoredFilename = "hilbert_unshifted_" + config.m_inputFile;
	if (!saveWav(shiftedFilename, sampleRate, shiftedSignal, 1)) {
		DBG_ERROR("Failed to save shifted WAV file.");
		return 1;
	}
	if (!saveWav(restoredFilename, sampleRate, restoredSignal, 1)) {
		DBG_ERROR("Failed to save unshifted WAV file.");
		return 1;
	}

	DBG_INFO("Shifted signal saved to " << shiftedFilename);
	DBG_INFO("Unshifted (restored) signal saved to " << restoredFilename);
	return 0;
}
