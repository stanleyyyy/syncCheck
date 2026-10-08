#include "AudioDeviceImpl.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace
{

std::string toLower(std::string str)
{
	std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
	return str;
}

bool parseBackend(const std::string& name, ma_backend& backend)
{
	for (int i = 0; i < ma_backend_null; i++) {
		if (toLower(ma_get_backend_name(static_cast<ma_backend>(i))) == toLower(name)) {
			backend = static_cast<ma_backend>(i);
			return true;
		}
	}
	return false;
}

} // namespace

MiniaudioDevice::~MiniaudioDevice()
{
	if (m_deviceInitialized) {
		ma_device_uninit(&m_device);
	}
	if (m_contextInitialized) {
		ma_context_uninit(&m_context);
	}
}

bool MiniaudioDevice::open(const std::string& spec, ma_device_config& config, std::string& error)
{
	std::string name = spec;
	ma_backend backend = ma_backend_null;
	bool hasBackend = false;

	// "BACKEND:NAME" selects a specific backend. Anything else (including "hw:0,0") is a plain name.
	const std::size_t colon = spec.find(':');
	if (colon != std::string::npos && parseBackend(spec.substr(0, colon), backend)) {
		hasBackend = true;
		name = spec.substr(colon + 1);
	}

	if (ma_context_init(hasBackend ? &backend : nullptr, hasBackend ? 1 : 0, nullptr, &m_context) != MA_SUCCESS) {
		error = "cannot initialize audio backend";
		return false;
	}
	m_contextInitialized = true;

	const bool isCapture = config.deviceType == ma_device_type_capture;
	ma_device_id deviceId;
	ma_device_id *pDeviceId = nullptr;

	if (hasBackend && backend == ma_backend_alsa && !name.empty() && name != "default") {
		// ALSA identifies devices by PCM name, so any name from asound.conf can be used directly.
		std::memset(&deviceId, 0, sizeof(deviceId));
		std::strncpy(deviceId.alsa, name.c_str(), sizeof(deviceId.alsa) - 1);
		pDeviceId = &deviceId;
	} else if (!name.empty() && name != "default") {
		ma_device_info *playbackInfos = nullptr;
		ma_device_info *captureInfos = nullptr;
		ma_uint32 playbackCount = 0;
		ma_uint32 captureCount = 0;
		if (ma_context_get_devices(&m_context, &playbackInfos, &playbackCount, &captureInfos, &captureCount) != MA_SUCCESS) {
			error = "cannot enumerate audio devices";
			return false;
		}

		ma_device_info *infos = isCapture ? captureInfos : playbackInfos;
		const ma_uint32 count = isCapture ? captureCount : playbackCount;

		// Prefer an exact match, then fall back to a case insensitive substring match.
		const ma_device_info *match = nullptr;
		for (ma_uint32 i = 0; i < count && !match; i++) {
			if (name == infos[i].name) {
				match = &infos[i];
			}
		}
		for (ma_uint32 i = 0; i < count && !match; i++) {
			if (toLower(infos[i].name).find(toLower(name)) != std::string::npos) {
				match = &infos[i];
			}
		}

		if (!match) {
			error = "no " + std::string(isCapture ? "capture" : "playback") + " device matches '" + name + "'";
			return false;
		}

		deviceId = match->id;
		pDeviceId = &deviceId;
	}

	if (isCapture) {
		config.capture.pDeviceID = pDeviceId;
	} else {
		config.playback.pDeviceID = pDeviceId;
	}

	if (ma_device_init(&m_context, &config, &m_device) != MA_SUCCESS) {
		error = "cannot open " + std::string(isCapture ? "capture" : "playback") + " device '" + spec + "'";
		return false;
	}
	m_deviceInitialized = true;
	return true;
}

std::vector<AudioDeviceInfo> AudioDevices::list(Direction direction)
{
	std::vector<AudioDeviceInfo> devices;

	ma_context context;
	if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
		return devices;
	}

	ma_device_info *playbackInfos = nullptr;
	ma_device_info *captureInfos = nullptr;
	ma_uint32 playbackCount = 0;
	ma_uint32 captureCount = 0;
	if (ma_context_get_devices(&context, &playbackInfos, &playbackCount, &captureInfos, &captureCount) == MA_SUCCESS) {
		const bool isCapture = direction == Direction::Capture;
		ma_device_info *infos = isCapture ? captureInfos : playbackInfos;
		const ma_uint32 count = isCapture ? captureCount : playbackCount;

		for (ma_uint32 i = 0; i < count; i++) {
			AudioDeviceInfo info;
			info.m_name = infos[i].name;
			info.m_backend = ma_get_backend_name(context.backend);
			info.m_isDefault = infos[i].isDefault;

			ma_device_info details;
			if (ma_context_get_device_info(&context, isCapture ? ma_device_type_capture : ma_device_type_playback, &infos[i].id, &details) == MA_SUCCESS) {
				for (ma_uint32 f = 0; f < details.nativeDataFormatCount; f++) {
					info.m_maxChannels = std::max<unsigned int>(info.m_maxChannels, details.nativeDataFormats[f].channels);
				}
			}
			devices.push_back(info);
		}
	}

	ma_context_uninit(&context);
	return devices;
}
