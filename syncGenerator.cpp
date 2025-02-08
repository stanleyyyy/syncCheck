#include "SyncCheck.h"
#include "TestSignalGenerator.h"
#include "Logger.h"
#include "nsdkStacktrace.h"
#include <getopt.h>
#include <signal.h>
#include <iostream>
#include <iomanip>

static volatile bool g_running = true;

struct AppConfig
{
	float duration = 5.0f;
	std::string outputPrefix = "test_signal";
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

void printUsage(const char *programName)
{
	std::cerr << "Usage: " << programName << " [options]\n"
			  << "Options:\n"
			  << "  -d, --duration=SECS    Signal duration in seconds (default: 5.0)\n"
			  << "  -o, --output=PREFIX    Output file prefix (default: test_signal)\n"
			  << "  -q, --quiet            Disable progress output\n"
			  << "  -r, --rate=RATE        Sample rate (default: 48000)\n"
			  << "      --block-size=NUM   FFT block size (default: 8192)\n"
			  << "      --overlap=NUM      Overlap percent (50 or 75, default: 50)\n"
			  << "      --bands=NUM        Number of frequency bands (default: 8)\n"
			  << "      --interp=NUM       Interpolation factor (default: 1)\n"
			  << "      --delays=DELAYS    Comma-separated list of delays in ms for each band\n"
			  << "  -h, --help             Show this help message\n";
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

std::pair<bool, AppConfig> parseCommandLine(int argc, char *argv[])
{
	AppConfig config;
	static struct option long_options[] = {
		{ "duration", required_argument, 0, 'd' },
		{ "output", required_argument, 0, 'o' },
		{ "quiet", no_argument, 0, 'q' },
		{ "rate", required_argument, 0, 'r' },
		{ "block-size", required_argument, 0, 1 },
		{ "overlap", required_argument, 0, 2 },
		{ "bands", required_argument, 0, 3 },
		{ "interp", required_argument, 0, 4 },
		{ "delays", required_argument, 0, 5 },
		{ "help", no_argument, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "d:o:qr:h", long_options, nullptr)) != -1) {
		switch (opt) {
		case 'd':
			config.duration = std::stof(optarg);
			break;
		case 'o':
			config.outputPrefix = optarg;
			break;
		case 'q':
			config.verbose = false;
			break;
		case 'r':
			config.sampleRate = std::stoi(optarg);
			break;
		case 1: // block-size
			config.blockSize = std::stoi(optarg);
			break;
		case 2: // overlap
			config.overlapPercent = std::stoi(optarg);
			if (config.overlapPercent != 50 && config.overlapPercent != 75) {
				DBG_ERROR("Invalid overlap percent. Must be 50 or 75");
				return { false, config };
			}
			break;
		case 3: // bands
			config.numBands = std::stoi(optarg);
			break;
		case 4: // interp
			config.interpFactor = std::stoi(optarg);
			break;
		case 5: // delays
			config.delays = parseDelays(optarg, config.numBands);
			if (config.delays.empty()) {
				return { false, config };
			}
			break;
		case 'h':
			return { false, config };
		default:
			return { false, config };
		}
	}

	// Validate parameters
	if (config.duration <= 0) {
		DBG_ERROR("Duration must be positive");
		return { false, config };
	}

	if (config.sampleRate <= 0) {
		DBG_ERROR("Sample rate must be positive");
		return { false, config };
	}

	return { true, config };
}

int main(int argc, char *argv[])
{
	// Install stack trace handler
	nsdkStacktrace::install();

	// Set desired log level
	Logger::getInstance().setLogLevel(LOG_DEBUG);

	if (argc < 2) {
		printUsage(argv[0]);
		return 1;
	}

	auto [configValid, config] = parseCommandLine(argc, argv);
	if (!configValid) {
		printUsage(argv[0]);
		return 1;
	}

	// Set up signal handling
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	// Initialize configuration
	SyncCheckConfig syncConfig(config.blockSize, config.overlapPercent, config.sampleRate,
							 config.numBands, config.interpFactor);

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
		if (!testSignalGenerator.renderAudioBlock(block, config.blockSize)) {
			DBG_ERROR("Error generating audio block");
			break;
		}

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
