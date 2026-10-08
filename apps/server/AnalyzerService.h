#pragma once

#include "SyncAnalyzer.h"
#include "json.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using fs_path = std::filesystem::path;

/**
 * @brief Settings edited in the web UI: analyzer settings plus the input selection.
 */
struct ServerSettings
{
	SyncAnalyzerSettings m_analyzer;

	/**
	 * @brief "device" or "file".
	 */
	std::string m_source = "device";

	/**
	 * @brief Audio file analyzed when m_source is "file".
	 */
	std::string m_file;

	/**
	 * @brief Record device input so that any moment of the session can be inspected later.
	 */
	bool m_record = true;

	/**
	 * @brief Play the captured audio on m_monitorDevice.
	 */
	bool m_monitor = false;
	std::string m_monitorDevice = "default";

	ServerSettings();
};

void to_json(nlohmann::json &j, const ServerSettings &settings);
void from_json(const nlohmann::json &j, ServerSettings &settings);

/**
 * @brief Runs measurement sessions for the web UI and keeps their history.
 *
 * A session lasts from start() until the input ends or stop() is called. Its measurements
 * stay available until the next session starts. Device input is recorded into the
 * session directory, file input is read in place; either way inspect() can look at any
 * moment of the session.
 */
class AnalyzerService
{
public:
	/**
	 * @param dataDir Directory for settings.json, sessions/ and uploads/.
	 */
	explicit AnalyzerService(const std::string &dataDir);
	~AnalyzerService();

	const std::string &dataDir() const { return m_dataDir; }

	ServerSettings settings() const;

	/**
	 * @brief Store new settings (used by the next session) and save them to disk.
	 */
	void setSettings(const ServerSettings &settings);

	/**
	 * @brief Restore and save the default settings.
	 */
	void resetSettings();

	bool start(std::string &error);
	void stop();

	/**
	 * @brief Stop the session and wake up all waitForChange() callers. Call before destruction.
	 */
	void shutdown();

	/**
	 * @brief Running state, status message, settings and session description.
	 */
	nlohmann::json state() const;

	/**
	 * @brief Measurements of the current session starting at row 'from', in columns.
	 */
	nlohmann::json measurements(std::size_t from) const;

	/**
	 * @brief Correlation and waveforms at a moment of the session.
	 *
	 * @param time Session time in seconds; ignored when live is set.
	 * @param rangeMs Correlation lag range (+/-) in ms.
	 * @param windowMs Waveform window in ms.
	 * @param offsetMs Center of the waveform window relative to the moment in ms.
	 * @param live Use the latest measurement instead of re-analyzing the audio.
	 * @param align Shift each compared channel's waveform by its measured delay.
	 */
	bool inspect(double time, double rangeMs, double windowMs, double offsetMs, bool live, bool align, nlohmann::json &result,
		std::string &error) const;

	/**
	 * @brief Waveforms of the current session's audio between two session times.
	 *
	 * @param from Start in seconds.
	 * @param to End in seconds.
	 * @param delaysMs Channels to read later by a delay in ms, so they line up with the reference.
	 */
	bool waveform(double from, double to, const std::map<unsigned int, double> &delaysMs, nlohmann::json &result,
		std::string &error) const;

	/**
	 * @brief Waveforms of the whole recording of the current session, in at most points buckets.
	 */
	bool overview(std::size_t points, nlohmann::json &result, std::string &error) const;

	/**
	 * @brief Audio of the current session (recording or input file), empty if there is none.
	 */
	std::string sessionAudio() const;

	/**
	 * @brief All sessions in the data directory, newest first.
	 */
	nlohmann::json listSessions() const;

	/**
	 * @brief Make a stored session the current one (for viewing, inspecting and playback).
	 */
	bool openSession(const std::string &key, std::string &error);

	/**
	 * @brief Change the name and/or the tags of a session.
	 * @param changes Object with "name" (string) and/or "tags" (array of strings).
	 */
	bool updateSession(const std::string &key, const nlohmann::json &changes, std::string &error);

	/**
	 * @brief Delete a session directory. The input file of a file session is kept.
	 */
	bool deleteSession(const std::string &key, std::string &error);

	/**
	 * @brief Measurements CSV of a session (empty key: the current session) and its download name.
	 */
	bool sessionCsv(const std::string &key, std::string &csv, std::string &filename, std::string &error) const;

	/**
	 * @brief Audio file of a session (empty key: the current session) and its download name.
	 */
	bool sessionAudioFile(const std::string &key, std::string &path, std::string &filename, std::string &error) const;

	/**
	 * @brief Disk usage: free space of the data directory and, while recording, size and write rate.
	 */
	nlohmann::json disk() const;

	/**
	 * @brief Position of an event stream client.
	 */
	struct Cursor
	{
		uint64_t m_version = 0;
		int m_session = -1;
		uint64_t m_stateVersion = 0;
		std::size_t m_row = 0;
		std::chrono::steady_clock::time_point m_lastDisk;
	};

	/**
	 * @brief Wait until something changed after cursor.m_version, the timeout expires or shutdown().
	 */
	void waitForChange(const Cursor &cursor, std::chrono::milliseconds timeout) const;

	/**
	 * @brief Server-Sent Events describing everything that changed since the cursor; advances the cursor.
	 */
	std::string eventsSince(Cursor &cursor) const;

	bool isShutDown() const;

private:
	struct Pair
	{
		FFTSyncCheck::ChannelAssignment m_assignment;
		std::vector<float> m_delay;
		std::vector<float> m_confidence;
		std::vector<float> m_pslr;
		std::vector<float> m_refRms;
		std::vector<float> m_sigRms;
	};

	struct Session
	{
		int m_id = 0;
		std::string m_key;		///< Directory name, also the identifier in exported file names.
		std::string m_name;
		std::vector<std::string> m_tags;
		std::string m_source;	///< "device" or "file"
		bool m_recorded = false;	///< m_audio is a recording made by this session
		std::chrono::steady_clock::time_point m_startedAt;
		std::unique_ptr<std::ofstream> m_csv;
		std::string m_dir;
		std::string m_audio;
		std::string m_started;
		std::string m_input;
		SyncAnalyzerSettings m_settings;
		unsigned int m_sampleRate = 0;
		unsigned int m_channels = 0;
		unsigned int m_interpFactor = 0;
		std::vector<double> m_time;
		std::vector<Pair> m_pairs;
		SyncAnalyzerSnapshot m_latest;
	};

	void onSnapshot(const SyncAnalyzerSnapshot &snapshot);
	void setStatus(const std::string &status, bool running);
	void notify();
	void saveSettings() const;
	void loadSettings();
	nlohmann::json stateLocked() const;
	nlohmann::json measurementsLocked(std::size_t from) const;
	nlohmann::json pairsLocked() const;
	std::string measurementsCsvLocked() const;
	void writeSessionInfoLocked() const;
	void appendCsvLocked();
	nlohmann::json diskLocked() const;
	bool sessionDir(const std::string &key, fs_path &dir, std::string &error) const;
	static std::string exportName(const std::string &key, const std::string &name, const std::string &suffix);

	const std::string m_dataDir;

	mutable std::mutex m_mutex;
	mutable std::condition_variable m_changed;
	uint64_t m_version = 1;
	uint64_t m_stateVersion = 1;
	bool m_shutdown = false;

	ServerSettings m_settings;
	SyncAnalyzer m_analyzer;
	std::thread m_worker;
	bool m_running = false;
	std::atomic<bool> m_stopRequested { false };
	std::string m_status = "Idle";
	Session m_session;
	int m_lastSessionId = 0;

	// Coarse min/max of the session audio for overview(); only new audio is read while recording.
	struct OverviewCache
	{
		std::string m_audio;
		unsigned int m_channels = 0;
		unsigned int m_sampleRate = 0;
		uint64_t m_frames = 0;		///< frames covered by whole buckets
		std::vector<float> m_min;	///< bucket * channels + channel
		std::vector<float> m_max;
	};
	mutable std::mutex m_overviewMutex;
	mutable OverviewCache m_overview;
};
