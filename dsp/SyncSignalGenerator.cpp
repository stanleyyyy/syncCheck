#include "SyncSignalGenerator.h"

template<>
float SyncSignalGenerator<float>::assignValue(uint32_t value)
{
	// First center around 0 by subtracting half range (2^(bitWidth-1))
	// Then divide by half range to normalize to [-1,1]
	int32_t centered = static_cast<int32_t>(value) - (1 << (m_bitWidth - 1));
	float normalized = static_cast<float>(centered) / (1 << (m_bitWidth - 1));
	return normalized;
}
