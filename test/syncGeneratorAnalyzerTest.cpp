#include "SyncCheck.h"
#include "TestSignalGenerator.h"
#include "Logger.h"

int main()
{
	// Set desired log level
	Logger::getInstance().setLogLevel(LOG_DEBUG);

	// Initialize configuration
	SyncCheckConfig config;

	// Create sync checker with config
	FFTSyncCheck syncChecker(config);

	const float TEST_DURATION = 5.0f;  // Duration in seconds
	const float BLOCK_DURATION = 0.1f; // Block duration in seconds

	// Define test delays for each band in milliseconds
	std::vector<float> test_delays = {
		0.0f,   // Band 0 (reference)
		-0.3f,  // Band 1
		1.0f,   // Band 2
		-0.5f,  // Band 3
		2.0f,   // Band 4
		-1.0f,  // Band 5
		0.7f,   // Band 6
		-1.3f   // Band 7
	};

	DBG_INFO("Generating test signal...");
	TestSignalGenerator testSignalGenerator(config);
	testSignalGenerator.enableWavDump(true, "test_signal");
	std::vector<float> test_signal = testSignalGenerator.renderTestSignal(TEST_DURATION, test_delays);
	DBG_INFO("Test signal generated, processing blocks...");

	float elapsed_time = 0.0f;
	for (size_t i = 0; i < test_signal.size(); i += static_cast<size_t>(BLOCK_DURATION * syncChecker.getSampleRate())) {
		size_t block_size = std::min(static_cast<size_t>(BLOCK_DURATION * syncChecker.getSampleRate()), test_signal.size() - i);
		std::vector<float> block(test_signal.begin() + i, test_signal.begin() + i + block_size);
		syncChecker.processSampleBlock(block);

		elapsed_time += BLOCK_DURATION;
		if (elapsed_time >= 1.0f) {
			syncChecker.printDelayStats(test_delays);
			elapsed_time = 0.0f;
		}
	}

	return 0;
}
