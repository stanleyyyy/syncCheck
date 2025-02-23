#pragma once

#include "SharedMemoryObject.h"

/**
 * @brief path to shared memory object descriptor
 */
#define AUDIO_SYNC_IPC_PATH "/tmp/audio_sync_ipc"

/**
 * @brief Maximum number of channel assignments that can be stored
 */
#define MAX_CHANNEL_ASSIGNMENTS 8

/**
 * @brief Maximum number of correlation samples that can be stored
 */
#define MAX_CORRELATION_SAMPLES 16384

/**
 * @brief Structure to store correlation data for a single channel assignment
 */
struct CorrelationData {
	// Channel assignment details
	std::size_t m_refChannel = 0;
	std::size_t m_refBand = 0;
	std::size_t m_sigChannel = 0;
	std::size_t m_sigBand = 0;

	// Correlation results
	float m_delay = 0.0f;
	float m_confidence = 0.0f;
	float m_pslr = 0.0f;
	float m_refRMS = 0.0f;
	float m_sigRMS = 0.0f;

	// Raw correlation data
	float m_correlationData[MAX_CORRELATION_SAMPLES];
	std::size_t m_correlationSize = 0;
	std::size_t m_sampleRate = 0;
	std::size_t m_interpFactor = 0;
};

/**
 * @brief Shared memory structure for audio synchronization data
 */
struct AudioSyncData {

	// Owner identification
	uint32_t m_ownerId = 0;

	// Update instance id for change tracking
	unsigned int m_instanceId = 0;

	// Number of valid assignments in m_correlations
	std::size_t m_numAssignments = 0;

	// Correlation data for each channel assignment
	CorrelationData m_correlations[MAX_CHANNEL_ASSIGNMENTS];
};

/**
 * @brief Reader class for audio sync shared memory
 */
class AudioSyncReaderWriter {
private:
	SharedMemoryObject<AudioSyncData> m_syncData;
	unsigned int m_lastInstanceId;

public:
	AudioSyncReaderWriter()
		: m_syncData(AUDIO_SYNC_IPC_PATH)
		, m_lastInstanceId(0)
	{}

	/**
	 * @brief Get current shared sync data
	 * @param data Output parameter for sync data
	 * @return true if data was updated since last call
	 */
	bool get(AudioSyncData &data)
	{
		data = m_syncData.get();

		if (data.m_instanceId != m_lastInstanceId) {
			m_lastInstanceId = data.m_instanceId;
			return true;
		}
		return false;
	}

	void set(AudioSyncData &config)
	{
		// atomically set the new value and increment instance id
		m_syncData.atomicAccess([&](AudioSyncData &atomicConfig) {

			// increment instance id (use current atomic instance id as a base to avoid race conditions)
			config.m_instanceId = atomicConfig.m_instanceId + 1;

			// store updated configuration to shared memory object
			atomicConfig = config;
		});

		m_lastInstanceId = config.m_instanceId;
	}
};

typedef SharedMemoryObject<AudioSyncData> SharedAudioSync;
