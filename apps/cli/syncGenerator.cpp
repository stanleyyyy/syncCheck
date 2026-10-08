#include "SyncCheck.h"
#include "TestSignalGenerator.h"
#include "Logger.h"
#include "CLI11.hpp"
#include <sstream>
#include <signal.h>
#include <iostream>
#include <iomanip>

static volatile bool g_running = true;

struct AppConfig
{
	float duration = 5.0f;
	std::string outputPrefix = "test_signal_mixed.wav";
	bool verbose = true;
	unsigned int sampleRate = 48000;
	unsigned int blockSize = SyncCheckConfig::DEFAULT_BLOCK_SIZE;
	unsigned int overlapPercent = SyncCheckConfig::DEFAULT_OVERLAP_PERCENT;
	unsigned int numBands = SyncCheckConfig::DEFAULT_NUM_BANDS;
	unsigned int interpFactor = SyncCheckConfig::DEFAULT_INTERP_FACTOR;
	std::vector<float> delays = {
		0.0f,   // Band 0 (reference)
		-0.3f,  // Band 1
		1.0f,   // Band 2
		-0.5f,  // Band 3
		2.0f,   // Band 4
		-1.0f,  // Band 5
		0.7f,   // Band 6
		-1.3f   // Band 7
	};
};

void signalHandler(int signum)
{
	g_running = false;
}

std::vector<float> parseDelays(const std::string& delaysStr, size_t numBands)
{
	std::vector<float> delays;
	std::stringstream ss(delaysStr);
	std::string delay;

	while (std::getline(ss, delay, ',')) {
		delays.push_back(std::stof(delay));
	}

	if (delays.size() != numBands) {
		DBG_ERROR("Number of delays (" << delays.size() << ") doesn't match number of bands (" << numBands << ")");
		return std::vector<float>();
	}

	return delays;
}

int main(int argc, char *argv[])
{
	AppConfig config;
	bool quiet = false;
	std::string delays;

	CLI::App app { "Generates a sync test signal with a configurable delay per frequency band." };
	app.option_defaults()->always_capture_default();
	app.get_formatter()->column_width(32);

	app.add_option("-d,--duration", config.duration, "Signal duration in seconds")->check(CLI::PositiveNumber);
	app.add_option("-o,--output", config.outputPrefix, "Output file prefix");
	app.add_flag("-q,--quiet", quiet, "Disable progress output");
	app.add_option("-r,--rate", config.sampleRate, "Sample rate")->check(CLI::PositiveNumber);
	app.add_option("--block-size", config.blockSize, "FFT block size")->check(CLI::PositiveNumber);
	app.add_option("--overlap", config.overlapPercent, "Overlap percent")->check(CLI::IsMember({ 50, 75 }));
	app.add_option("--bands", config.numBands, "Number of frequency bands");
	app.add_option("--interp", config.interpFactor, "Interpolation factor");
	app.add_option("--delays", delays, "Comma-separated list of delays in ms for each band");

	CLI11_PARSE(app, argc, argv);
	config.verbose = !quiet;

	if (!delays.empty()) {
		config.delays = parseDelays(delays, config.numBands);
		if (config.delays.empty()) {
			return 1;
		}
	}

	// Set desired log level
	Logger::getInstance().setLogLevel(LOG_DEBUG);
	// Set up signal handling
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);


	config.blockSize = 8192;
	config.overlapPercent = 50;
	config.numBands = 8;
	config.interpFactor = 1;

	// Initialize configuration
	SyncCheckConfig syncConfig(config.blockSize, config.overlapPercent, config.sampleRate,
							 config.numBands, config.interpFactor);
	syncConfig.initBands(16000, 8000, 200);

	DBG_INFO("Generating test signal with parameters:");
	DBG_INFO("Duration: " << config.duration << "s");
	DBG_INFO("Sample rate: " << config.sampleRate);
	DBG_INFO("Block size: " << config.blockSize);
	DBG_INFO("Overlap: " << config.overlapPercent << "%");
	DBG_INFO("Number of bands: " << config.numBands);
	DBG_INFO("Interpolation factor: " << config.interpFactor);
	DBG_INFO("Output prefix: " << config.outputPrefix);

	// Create test signal generator
	TestSignalGenerator testSignalGenerator(syncConfig);
	testSignalGenerator.enableWavDump(true, config.outputPrefix);

	// Initialize the generator with delays
	testSignalGenerator.init(config.delays);

	// Calculate total number of frames and blocks
	size_t totalFrames = static_cast<size_t>(config.duration * config.sampleRate);
	size_t framesProcessed = 0;
	size_t lastPercent = 0;

	// Process blocks
	std::vector<float> block(config.blockSize); // Allocate full block size buffer

	while (framesProcessed < totalFrames && g_running) {
		// Calculate remaining frames
		size_t remainingFrames = totalFrames - framesProcessed;
		size_t framesToProcess = std::min(config.blockSize, static_cast<unsigned int>(remainingFrames));

		// Generate block
		testSignalGenerator.renderAudioBlock(block);

		// Update progress
		framesProcessed += framesToProcess;
		if (config.verbose) {
			size_t percent = (framesProcessed * 100) / totalFrames;
			if (percent > lastPercent) {
				DBG_INFO_INLINE("\rProgress: [");
				// Print 50 characters total (= or spaces) based on percentage
				size_t equalSigns = (percent * 50) / 100;
				for (size_t i = 0; i < equalSigns; i++) {
					DBG_INFO_INLINE("=");
				}
				for (size_t i = equalSigns; i < 50; i++) {
					DBG_INFO_INLINE(" ");
				}
				DBG_INFO_INLINE("] " << std::setfill(' ') << std::setw(3) << percent << "%");
				lastPercent = percent;
			}
		}
	}

	if (config.verbose) {
		DBG_INFO_INLINE("\n");
		testSignalGenerator.getQualityMetrics().print();
	}

	if (!g_running) {
		DBG_INFO("Signal generation interrupted.");
		return 1;
	}

	DBG_INFO("Test signal generation completed.");
	return 0;
}
