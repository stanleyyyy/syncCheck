#pragma once

#include "kiss_fftr.h"
#include <vector>
#include <memory>

/**
 * @brief Simple C++ wrapper for KissFFT real FFT
 */
class KissFFTWrapper
{
public:
	/**
	 * @brief Constructor
	 * @param size FFT size (must be even)
	 * @return true if initialization successful, false otherwise
	 */
	explicit KissFFTWrapper(size_t size);

	/**
	 * @brief Destructor
	 */
	~KissFFTWrapper();

	/**
	 * @brief Perform forward real FFT
	 * @param input Real input data
	 * @param output Complex output data
	 * @return true if successful, false otherwise
	 */
	bool forward(const std::vector<float> &input, std::vector<kiss_fft_cpx> &output);

	/**
	 * @brief Perform inverse real FFT
	 * @param input Complex input data
	 * @param output Real output data
	 * @param scale If true, scales the output by 1/N
	 * @return true if successful, false otherwise
	 */
	bool inverse(const std::vector<kiss_fft_cpx> &input, std::vector<float> &output, bool scale = true);

	/**
	 * @brief Get the FFT size
	 * @return FFT size
	 */
	size_t getSize() const { return m_size; }

private:
	size_t m_size;
	kiss_fftr_cfg m_forwardCfg;
	kiss_fftr_cfg m_inverseCfg;
};
