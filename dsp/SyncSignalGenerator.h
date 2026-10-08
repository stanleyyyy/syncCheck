#pragma once

#include <bitset>
#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>

// Preprocessor defines for flexible bit-width LFSR (Linear Feedback Shift Register)
#define BIT_WIDTH 24                 // Width of the LFSR (e.g., 24 bits)
#define MSB_MASK (1 << (BIT_WIDTH - 1))  // Mask for the most significant bit (e.g., 0x800000 for 24 bits)
#define POLY_MASK ((1 << BIT_WIDTH) - 1) // Mask for limiting state to BIT_WIDTH bits (e.g., 0xFFFFFF)

class PRNGenerator
{
public:
	PRNGenerator(uint32_t seed = POLY_MASK)
		: m_state(seed), m_seed(seed), m_position(0)
	{
		initializeJumpTable();
	}

	// Generate a random bitWidth-bit value by generating 'bitWidth' consecutive bits
	uint32_t generateRandomChunk(uint32_t bitWidth)
	{
		uint32_t randomValue = 0;
		for (uint32_t i = 0; i < bitWidth; i++) {
			uint8_t bit = next();
			randomValue |= (bit << i);
		}
		return randomValue;
	}

	// Generate the next pseudorandom bit and advance the LFSR
	uint8_t next()
	{
		uint8_t bit = (m_state & MSB_MASK) >> (BIT_WIDTH - 1);
		uint32_t newBit = ((m_state >> (BIT_WIDTH - 1)) ^ (m_state >> (BIT_WIDTH - 2))) & 1;
		m_state = ((m_state << 1) | newBit) & POLY_MASK;
		++m_position;
		return bit;
	}

	// Set the generator to a specific position using a jump table computed via matrix exponentiation.
	void setPosition(uint32_t position)
	{
		if (position == m_position) {
			return;
		}

		// Reset to initial state
		uint32_t state = m_seed;
		uint32_t chunks = position / BIT_WIDTH;

		// Start with the identity matrix
		uint32_t acc[BIT_WIDTH];
		for (int i = 0; i < BIT_WIDTH; i++) {
			acc[i] = (1u << i);
		}

		int i = 0;
		while (chunks) {
			if (chunks & 1) {
				uint32_t temp[BIT_WIDTH];
				multiplyMatrix(acc, m_jumpTable[i], temp);
				for (int k = 0; k < BIT_WIDTH; k++) {
					acc[k] = temp[k];
				}
			}
			chunks >>= 1;
			i++;
		}

		// Apply the matrix to the seed
		state = multiplyMatrixVector(acc, m_seed);

		// Set the state and handle any remaining bits
		m_state = state;
		uint32_t remainingBits = position % BIT_WIDTH;
		for (uint32_t j = 0; j < remainingBits; j++) {
			next();
		}
		m_position = position;
	}

	uint32_t getPosition() const { return m_position; }

	void getContext(uint32_t &state, uint32_t &seed, uint32_t &position)
	{
		state = m_state;
		seed = m_seed;
		position = m_position;
	}

private:
	uint32_t m_state;
	const uint32_t m_seed;
	uint32_t m_position;

	static const int tableSize = 32;  // 32 entries for powers of 2 jumps
	uint32_t m_jumpTable[tableSize][BIT_WIDTH];

	void buildBaseMatrix(uint32_t base[BIT_WIDTH])
	{
		base[0] = (1u << (BIT_WIDTH - 1)) | (1u << (BIT_WIDTH - 2));  // XOR of MSB and second MSB
		for (int i = 1; i < BIT_WIDTH; i++) {
			base[i] = (1u << (i - 1));
		}
	}

	void multiplyMatrix(const uint32_t A[BIT_WIDTH], const uint32_t B[BIT_WIDTH], uint32_t C[BIT_WIDTH])
	{
		for (int i = 0; i < BIT_WIDTH; i++) {
			uint32_t row = 0;
			for (int k = 0; k < BIT_WIDTH; k++) {
				if (A[i] & (1u << k)) {
					row ^= B[k];
				}
			}
			C[i] = row;
		}
	}

	uint32_t multiplyMatrixVector(const uint32_t M[BIT_WIDTH], uint32_t v)
	{
		uint32_t result = 0;
		for (int i = 0; i < BIT_WIDTH; i++) {
			int parity = std::bitset<32>(M[i] & v).count() & 1;
			result |= (parity << i);
		}
		return result;
	}

	void initializeJumpTable()
	{
		uint32_t base[BIT_WIDTH];
		buildBaseMatrix(base);

		// Compute base^(BIT_WIDTH) using matrix exponentiation
		uint32_t resultMat[BIT_WIDTH];
		for (int i = 0; i < BIT_WIDTH; i++) {
			resultMat[i] = (1u << i);
		}
		uint32_t basePow[BIT_WIDTH];
		for (int i = 0; i < BIT_WIDTH; i++) {
			basePow[i] = base[i];
		}

		for (int exp = BIT_WIDTH; exp; exp >>= 1) {
			if (exp & 1) {
				uint32_t temp[BIT_WIDTH];
				multiplyMatrix(resultMat, basePow, temp);
				for (int i = 0; i < BIT_WIDTH; i++) {
					resultMat[i] = temp[i];
				}
			}
			uint32_t temp[BIT_WIDTH];
			multiplyMatrix(basePow, basePow, temp);
			for (int i = 0; i < BIT_WIDTH; i++) {
				basePow[i] = temp[i];
			}
		}

		// Set jumpTable[0] = T^(BIT_WIDTH)
		for (int i = 0; i < BIT_WIDTH; i++) {
			m_jumpTable[0][i] = resultMat[i];
		}

		// Populate the rest of the jump table by squaring
		for (int j = 1; j < tableSize; j++) {
			multiplyMatrix(m_jumpTable[j - 1], m_jumpTable[j - 1], m_jumpTable[j]);
		}
	}
};

template<typename Type>
class SyncSignalGenerator
{
public:
	SyncSignalGenerator(uint32_t audioSampleRate, uint32_t syncBitRate, uint32_t bitWidth, uint32_t seed = 0xFFFFFF)
		: m_prn(seed),
		  m_audioSampleRate(audioSampleRate),
		  m_syncBitRate(syncBitRate),
		  m_bitWidth(bitWidth),
		  m_downsamplingRatio(audioSampleRate / syncBitRate),
		  m_currentPhase(0),
		  m_currentValue(0)  // Initialize current value
	{
	}

	void generateSyncStream(std::vector<Type> &syncStream, uint32_t startPosition)
	{
		// Calculate the phase offset within the downsampling ratio
		m_currentPhase = startPosition % m_downsamplingRatio;

		// Handle initial partial block if we're not phase-aligned
		uint32_t i = 0;
		if (m_currentPhase != 0) {
			uint32_t remainingInBlock = m_downsamplingRatio - m_currentPhase;
			uint32_t samplesToFill = std::min(remainingInBlock, static_cast<uint32_t>(syncStream.size()));

			for (; i < samplesToFill; ++i) {
				syncStream[i] = m_currentValue;
			}
			m_currentPhase = (m_currentPhase + samplesToFill) % m_downsamplingRatio;
		}

		// Generate sync values at the downsampled rate for the remaining samples
		for (; i < syncStream.size(); i++) {
			if (m_currentPhase == 0) {
				m_prn.setPosition(startPosition * m_bitWidth / m_downsamplingRatio);
				m_currentValue = assignValue(m_prn.generateRandomChunk(m_bitWidth));
			}
			syncStream[i] = m_currentValue;
			m_currentPhase = (m_currentPhase + 1) % m_downsamplingRatio;
			startPosition++;
		}
	}

	Type assignValue(uint32_t value)
	{
		return static_cast<Type>(value);
	}

private:
	PRNGenerator m_prn;
	uint32_t m_audioSampleRate;
	uint32_t m_syncBitRate;
	uint32_t m_bitWidth;
	uint32_t m_downsamplingRatio;
	uint32_t m_currentPhase;       // Track current phase within downsampling ratio
	Type m_currentValue;       // Store current PRN value
};

// Defined here, inline: MSVC does not link an explicit specialization defined in a separate source file.
template<>
inline float SyncSignalGenerator<float>::assignValue(uint32_t value)
{
	// First center around 0 by subtracting half range (2^(bitWidth-1))
	// Then divide by half range to normalize to [-1,1]
	int32_t centered = static_cast<int32_t>(value) - (1 << (m_bitWidth - 1));
	float normalized = static_cast<float>(centered) / (1 << (m_bitWidth - 1));
	return normalized;
}
