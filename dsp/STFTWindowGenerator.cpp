#include "STFTWindowGenerator.h"
#include "Logger.h"

STFTWindowGenerator::STFTWindowGenerator(int windowLength, int overlap, WindowType type)
{
	ASSERT_WITH_ERROR(overlap < windowLength, "Overlap must be less than window length.");
	ASSERT_WITH_ERROR(overlap == windowLength / 2 || overlap == (3 * windowLength) / 4, "Overlap must be 50% or 75% of window length.");

	m_windowLength = windowLength;
	m_overlap = overlap;
	m_hopSize = m_windowLength - m_overlap;
	m_type = type;
	generateWindows();
}

STFTWindowGenerator::~STFTWindowGenerator() = default;