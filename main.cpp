#include "SyncCheck.h"

int main()
{
	FFTSyncCheck syncChecker;
	const float TEST_DURATION = 5.0f;  // Duration in seconds.
	const float BLOCK_DURATION = 0.1f; // Block duration in seconds.

	// Define test delays for each band in milliseconds.
	std::vector<float> test_delays = {
		0.0f,  // Band 0 (reference, must be zero)
		0.5f,  // Band 1: 0.5ms delay
		1.0f,  // Band 2: 1.0ms delay
		-0.5f, // Band 3: 0.5ms ahead
		30.0f,  // Band 4: 2.0ms delay
		-1.0f, // Band 5: 1.0ms ahead
		0.7f,  // Band 6: 0.7ms delay
		-0.3f  // Band 7: 0.3ms ahead
	};

	printf("Generating test signal...\n");
	std::vector<float> test_signal = syncChecker.generateTestSignal(TEST_DURATION, test_delays);
	printf("Test signal generated, processing blocks...\n\n");

	float elapsed_time = 0.0f;
	for (size_t i = 0; i < test_signal.size(); i += static_cast<size_t>(BLOCK_DURATION * syncChecker.m_sampleRate)) {
		size_t block_size = std::min(static_cast<size_t>(BLOCK_DURATION * syncChecker.m_sampleRate), test_signal.size() - i);
		std::vector<float> block(test_signal.begin() + i, test_signal.begin() + i + block_size);
		syncChecker.processSampleBlock(block);

		elapsed_time += BLOCK_DURATION;
		if (elapsed_time >= 1.0f) {
			syncChecker.printDelayStats();
			printf("\nDelay Measurement Accuracy:\n");
			printf("Band | Expected (ms) | Measured (ms) | Error (ms)\n");
			printf("-----+--------------+--------------+-----------\n");
			for (int band = 1; band < FFTSyncCheck::NUM_BANDS; band++) {
				float measured = syncChecker.getFilteredDelay(band) * 1000.0f;
				float error = measured - test_delays[band];
				printf("%4d | %+11.3f | %+11.3f | %+8.3f\n",
					band, test_delays[band], measured, error);
			}
			printf("\n");
			elapsed_time = 0.0f;
		}
	}

	return 0;
}
