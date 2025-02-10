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
	// Calculate band boundaries in upper 20% of frequency range
	const std::size_t total_bins = m_fftSize / 2;
	const std::size_t start_bin = total_bins * 0.8; // Start at 80% of range
	const std::size_t usable_bins = total_bins - start_bin;
	const std::size_t bins_per_band = usable_bins / m_numBands;
	const std::size_t safety_gap = bins_per_band / 8;

	for (std::size_t i = 0; i < m_numBands; i++) {
		m_bands[i].m_startBin = start_bin + (i * bins_per_band);
		m_bands[i].m_endBin = start_bin + ((i + 1) * bins_per_band) - safety_gap - 1;
	}
}