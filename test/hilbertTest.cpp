#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <getopt.h>
#include "Logger.h"
#include "HilbertShifter.h"
#include <sndfile.h>

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

void signalHandler(int signum)
{
	// Handle signal
}

void printUsage(const char *programName)
{
	DBG_INFO("Usage: " << programName << " [options] input.wav\n"
			  << "Options:\n"
			  << "      --block-size=NUM   FFT block size (default: 8192)\n"
			  << "      --overlap=NUM      Overlap percent (50 or 75, default: 75)\n"
			  << "      --start-freq=NUM   Start frequency in Hz (default: 0)\n"
			  << "      --stop-freq=NUM    Stop frequency in Hz (default: 2000)\n"
			  << "      --target-freq=NUM  Target start frequency in Hz (default: 12000)\n"
			  << "  -h, --help            Show this help message\n");
}

std::pair<bool, AppConfig> parseCommandLine(int argc, char *argv[])
{
	AppConfig config;

	static struct option long_options[] = {
		{ "block-size",		required_argument, 0, 1 },
		{ "overlap",		required_argument, 0, 2 },
		{ "start-freq",		required_argument, 0, 3 },
		{ "stop-freq",		required_argument, 0, 4 },
		{ "target-freq",	required_argument, 0, 5 },
		{ "help",			no_argument,	   0, 'h' },
		{ 0, 0, 0, 0 }
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "h", long_options, nullptr)) != -1) {
		switch (opt) {
		case 1: // block-size
			config.m_blockSize = std::stoi(optarg);
			break;
		case 2: // overlap
			config.m_overlapPercent = std::stoi(optarg);
			if (config.m_overlapPercent != 50 && config.m_overlapPercent != 75) {
				DBG_ERROR("Invalid overlap percent. Must be 50 or 75");
				return { false, config };
			}
			break;
		case 3: // start-freq
			config.m_startFreq = std::stof(optarg);
			break;
		case 4: // stop-freq
			config.m_stopFreq = std::stof(optarg);
			break;
		case 5: // target-freq
			config.m_targetStart = std::stof(optarg);
			break;
		case 'h':
			return { false, config };
		default:
			return { false, config };
		}
	}

	// Get input filename from remaining argument
	if (optind >= argc) {
		DBG_ERROR("Input WAV file must be specified");
		return { false, config };
	}
	config.m_inputFile = argv[optind];

	return { true, config };
}

/**
 * @brief Load WAV file using libsndfile.
 * If the file has multiple channels, this function converts it to mono by taking the first channel.
 */
bool loadWav(const string &filename, int &sampleRate, vector<float> &data)
{
	SF_INFO sfInfo;
	sfInfo.format = 0;
	SNDFILE *infile = sf_open(filename.c_str(), SFM_READ, &sfInfo);
	if (!infile) {
		DBG_ERROR("Error opening file: " << filename);
		return false;
	}
	sampleRate = sfInfo.samplerate;
	// Read all channels
	vector<float> temp(sfInfo.frames * sfInfo.channels);
	sf_read_float(infile, temp.data(), temp.size());
	sf_close(infile);
	// Convert to mono by taking the first channel.
	data.resize(sfInfo.frames);
	for (int i = 0; i < sfInfo.frames; i++) {
		data[i] = temp[i * sfInfo.channels] / 4;
	}
	return true;
}

/**
 * @brief Save WAV file using libsndfile.
 */
bool saveWav(const string &filename, int sampleRate, const vector<float> &data, int channels)
{
	SF_INFO sfInfo;
	sfInfo.channels = channels;
	sfInfo.samplerate = sampleRate;
	sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	SNDFILE *outfile = sf_open(filename.c_str(), SFM_WRITE, &sfInfo);
	if (!outfile) {
		DBG_ERROR("Error creating file: " << filename);
		return false;
	}
	sf_write_float(outfile, data.data(), data.size());
	sf_close(outfile);
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
	if (argc < 2) {
		printUsage(argv[0]);
		return 1;
	}

	auto [configValid, config] = parseCommandLine(argc, argv);
	if (!configValid) {
		printUsage(argv[0]);
		return 1;
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
