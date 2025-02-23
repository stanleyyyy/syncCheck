#include "KissFFTComplexWrapper.h"
#include <cstdlib>

/**
 * @brief Constructor
 *
 * Allocates KissFFT configurations for forward and inverse transforms.
 */
KissFFTComplexWrapper::KissFFTComplexWrapper(size_t size) :
	m_size(size),
	m_forwardCfg(nullptr),
	m_inverseCfg(nullptr)
{
	m_forwardCfg = kiss_fft_alloc(m_size, 0, nullptr, 0);
	m_inverseCfg = kiss_fft_alloc(m_size, 1, nullptr, 0);
}

/**
 * @brief Destructor
 *
 * Frees the allocated KissFFT configurations.
 */
KissFFTComplexWrapper::~KissFFTComplexWrapper()
{
	if(m_forwardCfg)
		free(m_forwardCfg);
	if(m_inverseCfg)
		free(m_inverseCfg);
}

/**
 * @brief Perform forward complex FFT
 *
 * @param input Complex input data (size must equal FFT size)
 * @param output Complex output data (will be resized to FFT size)
 * @return true if FFT computation was successful, false otherwise
 */
bool KissFFTComplexWrapper::forward(const std::vector<kiss_fft_cpx> &input, std::vector<kiss_fft_cpx> &output)
{
	if(input.size() != m_size)
		return false;

	output.resize(m_size);
	kiss_fft(m_forwardCfg, input.data(), output.data());
	return true;
}

/**
 * @brief Perform inverse complex FFT
 *
 * @param input Complex input data (size must equal FFT size)
 * @param output Complex output data (will be resized to FFT size)
 * @param scale If true, scales the output by 1/N (N = FFT size)
 * @return true if FFT computation was successful, false otherwise
 */
bool KissFFTComplexWrapper::inverse(const std::vector<kiss_fft_cpx> &input, std::vector<kiss_fft_cpx> &output, bool scale)
{
	if(input.size() != m_size)
		return false;

	output.resize(m_size);
	kiss_fft(m_inverseCfg, input.data(), output.data());

	if(scale)
	{
		for (size_t i = 0; i < m_size; ++i)
		{
			output[i].r /= static_cast<float>(m_size);
			output[i].i /= static_cast<float>(m_size);
		}
	}
	return true;
}