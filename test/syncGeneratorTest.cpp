#include "SyncSignalGenerator.h"
#include "Logger.h"
#include <iomanip>
#include <iostream>

void testSequentialVsPositionGeneration()
{
	// The number of bits of the value returned from the PRNGenerator.
	// Please note that this is NOT THE SAME AS THE BIT WIDTH OF THE LFSR.
	const uint32_t RND_RESOLUTION = 24;

	// Create two generators with the same seed
	const uint32_t seed = 0xABCDEF;
	PRNGenerator gen1(seed);
	PRNGenerator gen2(seed);

	// Generate first 64 values sequentially with gen1
	std::vector<uint32_t> sequential_values;
	for (uint32_t i = 0; i < 64; i++) {
		sequential_values.push_back(gen1.generateRandomChunk(RND_RESOLUTION));
		uint32_t state, seed, position;
		gen1.getContext(state, seed, position);
		DBG_INFO(std::setw(2) << i << ": Sequential value: " << std::setw(8) << sequential_values.back() << " (0x" << std::hex << sequential_values.back() << ")");
		DBG_INFO(std::setw(2) << i << ": State: " << std::setw(8) << state << " (0x" << std::hex << state << ")");
		DBG_INFO(std::setw(2) << i << ": Seed: " << std::setw(8) << seed << " (0x" << std::hex << seed << ")");
		DBG_INFO(std::setw(2) << i << ": Position: " << std::setw(8) << position << " (0x" << std::hex << position << ")" << "\n");
	}

	// Test different positions with gen2
	std::vector<uint32_t> test_positions = {1, 5, 10};

	for (uint32_t pos : test_positions) {
		// Set generator to specific position
		gen2.setPosition(pos * RND_RESOLUTION);

		// Generate one value and compare with sequential
		uint32_t pos_value = gen2.generateRandomChunk(RND_RESOLUTION);
		uint32_t seq_value = sequential_values[pos];

		if (pos_value != seq_value) {
			DBG_ERROR("Mismatch at position: " << pos);
			DBG_ERROR("Sequential value: " << std::setw(8) << seq_value << " (0x" << std::hex << seq_value << ")");
			DBG_ERROR("Position value: " << std::setw(8) << pos_value << " (0x" << std::hex << pos_value << ")");
			ASSERT_WITH_ERROR(false, "Values don't match!");
		}
	}

	// Test consecutive positions
	gen2.setPosition(50 * RND_RESOLUTION);
	for (uint32_t i = 50; i < 60; i++) {
		uint32_t pos_value = gen2.generateRandomChunk(RND_RESOLUTION);
		uint32_t seq_value = sequential_values[i];

		if (pos_value != seq_value) {
			DBG_ERROR("Mismatch in consecutive test at position: " << i);
			DBG_ERROR("Sequential value: 0x" << std::hex << seq_value);
			DBG_ERROR("Position value:   0x" << std::hex << pos_value);
			ASSERT_WITH_ERROR(false, "Consecutive values don't match!");
		}
	}

	DBG_INFO("All position tests passed successfully!");
}

int main()
{
	testSequentialVsPositionGeneration();
	return 0;
}
