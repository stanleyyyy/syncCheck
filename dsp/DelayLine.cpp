#include "DelayLine.h"
#include <algorithm>
#include <cstring>
#include "Logger.h"

DelayLine::DelayLine(int maxDelay) :
	m_writePos(0),
	m_maxDelay(maxDelay)
{
	// Buffer size needs to accommodate both positive and negative delays
	m_buffer.resize(2 * maxDelay + 1, 0.0f);
	clear();
}

float DelayLine::process(float input, int delay)
{
	// Ensure delay is within bounds
	ASSERT_WITH_ERROR((delay > -m_maxDelay) && (delay < m_maxDelay), "Delay is out of bounds.");

	// Write input to buffer
	m_buffer[m_writePos % m_buffer.size()] = input;

	// Get reading position at specified delay
	const int readPos = m_buffer[(m_writePos - m_buffer.size() / 2 - delay) % m_buffer.size()];
	m_writePos++;
	return m_buffer[readPos];
}

void DelayLine::processBlock(const std::vector<float> &inputData, std::vector<float> &outputData, const std::size_t framesToProcess, const int delay)
{
	// Ensure delay is within bounds
	ASSERT_WITH_ERROR((delay > -m_maxDelay) && (delay < m_maxDelay), "Delay is out of bounds.");
	ASSERT_WITH_ERROR(inputData.size() >= framesToProcess, "Input data size must be greater than or equal to frames to process.");
	ASSERT_WITH_ERROR(outputData.size() >= framesToProcess, "Output data size must be greater than or equal to frames to process.");

	// Process each sample in the block
	for (size_t i = 0; i < framesToProcess; ++i)
	{
		// Write input to buffer
		m_buffer[m_writePos % m_buffer.size()] = inputData[i];

		// Read back delayed data
		outputData[i] = m_buffer[(m_writePos - m_buffer.size() / 2 - delay) % m_buffer.size()];

		// Update write position for next sample
		m_writePos++;
	}
}

void DelayLine::clear()
{
	std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
	m_writePos = 0;
}