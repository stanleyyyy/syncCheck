#pragma once
#include <vector>
/**
 * @brief A delay line implementation that can handle both positive and negative delays
 */
class DelayLine {
public:
	/**
	 * @brief Constructor
	 * @param maxDelay Maximum delay in samples (positive or negative)
	 */
	explicit DelayLine(int maxDelay);

	/**
	 * @brief Process a single sample through the delay line
	 * @param input Input sample
	 * @param delay Current delay in samples (can be positive or negative)
	 * @return Delayed sample
	 */
	float process(float input, int delay);

	/**
	 * @brief Process a block of samples through the delay line
	 * @param inputData Input buffer
	 * @param outputData Output buffer
	 * @param framesToProcess Number of frames to process
	 * @param delay Current delay in samples (can be positive or negative)
	 */
	void processBlock(const std::vector<float> &inputData, std::vector<float> &outputData, const std::size_t framesToProcess, const int delay);

	/**
	 * @brief Clear the delay line buffer
	 */
	void clear();

private:
	std::vector<float> m_buffer;	///< Circular buffer for delay line
	int m_writePos;				///< Current write position in buffer
	int m_maxDelay;				///< Maximum delay in samples
};