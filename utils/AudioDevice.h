#pragma once

#include <string>
#include <vector>

/**
 * @brief Description of an audio device as reported by the audio backend.
 */
struct AudioDeviceInfo
{
	std::string m_name;		///< Human readable device name, also accepted as a device specification.
	std::string m_backend;	///< Name of the backend providing the device (e.g. "ALSA", "PulseAudio", "WASAPI").
	bool m_isDefault = false;
};

/**
 * @brief Audio device enumeration.
 *
 * Device specifications accepted by DeviceCapture / DevicePlayback:
 *  - "" or "default"     System default device.
 *  - "alsa:PCM"          Raw ALSA PCM name, e.g. "alsa:hw:0,0" or "alsa:aux_plug".
 *  - "BACKEND:NAME"      Device NAME on a specific backend, e.g. "pulseaudio:default" or "wasapi:Microphone".
 *  - "NAME"              Device name, or a case insensitive part of it.
 */
namespace AudioDevices
{
	enum class Direction { Capture, Playback };

	/**
	 * @brief List available devices.
	 * @param direction Capture or playback devices.
	 * @return Devices of the default backend.
	 */
	std::vector<AudioDeviceInfo> list(Direction direction);
}
