#include "SyncCheckConfig.h"

SyncCheckConfig::SyncCheckConfig(std::size_t fft_size, std::size_t overlap_percent,
					std::size_t sample_rate, std::size_t num_bands, std::size_t interp_factor)
	: m_fftSize(fft_size)
	, m_overlapSize(fft_size * overlap_percent / 100)
	, m_sampleRate(sample_rate)
	, m_numBands(num_bands)
	, m_interpFactor(interp_factor)
	, m_bands(num_bands)
{
	initDefaultBands();
}

void SyncCheckConfig::setBands(const std::vector<SyncCheckConfig::Band>& bands)
{
	if (!bands.empty()) {
		m_bands = bands;
		m_numBands = bands.size();
	}
}

void SyncCheckConfig::initDefaultBands()
{
	// Calculate band boundaries
	const std::size_t bins_per_band = (m_fftSize / 2) / m_numBands;
	const std::size_t safety_gap = bins_per_band / 8;

	for (std::size_t i = 0; i < m_numBands; i++) {
		m_bands[i].m_startBin = i * bins_per_band;
		m_bands[i].m_endBin = (i + 1) * bins_per_band - safety_gap - 1;
	}
}