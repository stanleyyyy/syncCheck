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

bool SyncCheckConfig::operator == (const SyncCheckConfig &other) const
{
	return m_fftSize == other.m_fftSize &&
		m_overlapSize == other.m_overlapSize &&
		m_sampleRate == other.m_sampleRate &&
		m_numBands == other.m_numBands &&
		m_interpFactor == other.m_interpFactor &&
		m_bands == other.m_bands;
}

bool SyncCheckConfig::Band::operator == (const Band &other) const
{
	return m_startFreq == other.m_startFreq &&
		m_endFreq == other.m_endFreq;
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
	const double nyquist_freq = m_sampleRate / 2.0;
	const double start_freq = nyquist_freq * 0.8; // Start at 80% of range
	const double usable_freq_range = nyquist_freq - start_freq;
	const double freq_per_band = usable_freq_range / m_numBands;
	const double safety_gap = freq_per_band / 8;

	initBands(start_freq, usable_freq_range, safety_gap);
}

void SyncCheckConfig::initBands(const double start_freq, const double freq_range, const double band_sep)
{
	m_bands.resize(m_numBands);
	const double freq_per_band = freq_range / m_numBands;
	for (std::size_t i = 0; i < m_numBands; i++) {
		m_bands[i].m_startFreq = start_freq + (i * freq_per_band);
		m_bands[i].m_endFreq = start_freq + ((i + 1) * freq_per_band) - band_sep;
	}
}