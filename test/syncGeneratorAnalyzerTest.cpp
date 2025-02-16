#include "SyncCheck.h"
#include "TestSignalGenerator.h"
#include "Logger.h"

int main()
{
	// Set desired log level
	Logger::getInstance().setLogLevel(LOG_DEBUG);

	// Initialize configuration
	SyncCheckConfig config(8192, 50, 44100, 8, 1);
	config.initBands(18000, 4000, 100);

	// Create sync checker with config
	FFTSyncCheck syncChecker(config);

	// Define test delays for each channel assignment (default assignments are created for bands 0..7)
	std::vector<float> test_delays = {
		0.0f,   // Assignment 0 (corresponding to band 1)
		-0.3f,  // Assignment 1 (corresponding to band 2)
		1.0f,   // Assignment 2 (corresponding to band 3)
		-0.5f,  // Assignment 3 (corresponding to band 4)
		2.0f,   // Assignment 4 (corresponding to band 5)
		-1.0f,  // Assignment 5 (corresponding to band 6)
		0.7f,   // Assignment 6 (corresponding to band 7)
		-1.3f   // Assignment 7 (corresponding to band 8)
	};

	DBG_INFO("Generating test signal...");
	TestSignalGenerator testSignalGenerator(config);
	testSignalGenerator.enableWavDump(true, "test_signal");
	std::vector<float> test_signal = testSignalGenerator.renderTestSignal(5.0f, test_delays);
	DBG_INFO("Test signal generated, processing blocks...");

	float elapsed_time = 0.0f;
	const float BLOCK_DURATION = 0.1f;
	for (size_t i = 0; i < test_signal.size(); i += static_cast<size_t>(BLOCK_DURATION * syncChecker.getSampleRate()))
	{
		size_t block_size = std::min(static_cast<size_t>(BLOCK_DURATION * syncChecker.getSampleRate()), test_signal.size() - i);
		std::vector<float> block(test_signal.begin() + i, test_signal.begin() + i + block_size);
		syncChecker.processSampleBlock(block);

		elapsed_time += BLOCK_DURATION;
		if (elapsed_time >= 1.0f)
		{
			syncChecker.printDelayStats(test_delays);
			elapsed_time = 0.0f;
		}
	}

	return 0;
}