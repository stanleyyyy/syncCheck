#include "KissFFTWrapper.h"
#include <cstring>

KissFFTWrapper::KissFFTWrapper(size_t size)
	: m_size(size)
	, m_forwardCfg(nullptr)
	, m_inverseCfg(nullptr)
{
	m_forwardCfg = kiss_fftr_alloc(m_size, 0, nullptr, nullptr);
	m_inverseCfg = kiss_fftr_alloc(m_size, 1, nullptr, nullptr);
}

KissFFTWrapper::~KissFFTWrapper()
{
	if (m_forwardCfg) {
		kiss_fftr_free(m_forwardCfg);
	}
	if (m_inverseCfg) {
		kiss_fftr_free(m_inverseCfg);
	}
}

bool KissFFTWrapper::forward(const std::vector<float> &input, std::vector<kiss_fft_cpx> &output)
{
	if (!m_forwardCfg || input.size() != m_size) {
		return false;
	}

	output.resize(m_size / 2 + 1);
	kiss_fftr(m_forwardCfg, input.data(), output.data());
	return true;
}

bool KissFFTWrapper::inverse(const std::vector<kiss_fft_cpx> &input, std::vector<float> &output, bool scale)
{
	if (!m_inverseCfg || input.size() != m_size / 2 + 1) {
		return false;
	}

	output.resize(m_size);
	kiss_fftri(m_inverseCfg, input.data(), output.data());

	if (scale) {
		float scaleFactor = 1.0f / m_size;
		for (auto &val : output) {
			val *= scaleFactor;
		}
	}

	return true;
}