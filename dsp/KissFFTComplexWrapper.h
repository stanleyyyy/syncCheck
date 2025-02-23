#pragma once

#include "kiss_fft.h"
#include <vector>
#include <memory>

/**
 * @brief Simple C++ wrapper for KissFFT complex FFT
 */
class KissFFTComplexWrapper
{
public:
	/**
	 * @brief Constructor
	 * @param size FFT size
	 *
	 * Initializes the configurations for forward and inverse complex FFT.
	 */
	explicit KissFFTComplexWrapper(size_t size);

	/**
	 * @brief Destructor
	 */
	~KissFFTComplexWrapper();

	/**
	 * @brief Perform forward complex FFT
	 * @param input Complex input data (size must equal FFT size)
	 * @param output Complex output data (will be resized to FFT size)
	 * @return true if FFT computation was successful, false otherwise
	 */
	bool forward(const std::vector<kiss_fft_cpx> &input, std::vector<kiss_fft_cpx> &output);

	/**
	 * @brief Perform inverse complex FFT
	 * @param input Complex input data (size must equal FFT size)
	 * @param output Complex output data (will be resized to FFT size)
	 * @param scale If true, scales the output by 1/N (N = FFT size)
	 * @return true if FFT computation was successful, false otherwise
	 */
	bool inverse(const std::vector<kiss_fft_cpx> &input, std::vector<kiss_fft_cpx> &output, bool scale = true);

	/**
	 * @brief Get the FFT size
	 * @return FFT size
	 */
	size_t getSize() const { return m_size; }

private:
	size_t m_size;
	kiss_fft_cfg m_forwardCfg;
	kiss_fft_cfg m_inverseCfg;
};