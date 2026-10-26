#pragma once

#include "AudioDevice.h"
#include "miniaudio.h"
#include <string>

/**
 * @brief Owns a miniaudio context + device pair opened from a device specification.
 */
struct MiniaudioDevice
{
	ma_context m_context;
	ma_device m_device;
	bool m_contextInitialized = false;
	bool m_deviceInitialized = false;
	bool m_exclusive = false;	///< Device was opened in WASAPI exclusive mode.

	~MiniaudioDevice();

	/**
	 * @brief Resolve the device specification and initialize the device.
	 * @param spec Device specification, see AudioDevices.
	 * @param config Device configuration (device id is filled in here).
	 * @param error Receives a description of the failure.
	 * @param preferExclusive On WASAPI, try exclusive mode first and fall back to shared mode.
	 *        Shared mode goes through the Windows mixer, which may expose fewer channels than the
	 *        device has (e.g. a 4 channel interface delivered as stereo).
	 * @return True on success.
	 */
	bool open(const std::string& spec, ma_device_config& config, std::string& error, bool preferExclusive = false);
};
