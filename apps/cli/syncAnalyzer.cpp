#include <signal.h>
#include <iostream>
#include <iomanip>

#include "CLI11.hpp"
#include "SyncAnalyzer.h"
#include "AudioDevice.h"
#include "Logger.h"
#include "DelayStatsVisualizer.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

static SyncAnalyzer g_analyzer;

void signalHandler(int signum)
{
	g_analyzer.stop();
}

static void enableAnsiColors()
{
#ifdef _WIN32
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD mode = 0;
	if (GetConsoleMode(handle, &mode)) {
		SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	}
#endif
}

static void listDevices()
{
	for (auto direction : { AudioDevices::Direction::Capture, AudioDevices::Direction::Playback }) {
		std::cout << (direction == AudioDevices::Direction::Capture ? "Capture" : "Playback") << " devices:\n";
		for (const auto &device : AudioDevices::list(direction)) {
			std::cout << "  " << (device.m_isDefault ? "* " : "  ") << device.m_name << " [" << device.m_backend << "]\n";
		}
	}
}

int main(int argc, char *argv[])
{
	SyncAnalyzerSettings settings;
	std::string dumpStatsFileName;
	bool noColors = false;
	bool listDevicesOnly = false;

	CLI::App app { "Measures audio synchronization between frequency bands / channels of a test signal." };
	app.option_defaults()->always_capture_default();
	app.get_formatter()->column_width(32);

	app.add_option("-d,--device", settings.m_captureDevice,
		"Capture device: \"default\", device name (or part of it), \"BACKEND:NAME\", \"alsa:PCM\" or \"file://PATH\"");
	app.add_option("-r,--rate", settings.m_sampleRate, "Sample rate");
	app.add_option("-o,--output", dumpStatsFileName, "Output statistics to file (CSV if the name ends with .csv)");
	app.add_option("-O,--output-device", settings.m_playbackDevice, "Monitoring output device (same syntax as --device)");
	app.add_flag("-n,--no-colors", noColors, "Disable ANSI colors in output");
	app.add_option("-c,--channels", settings.m_channels, "Number of input channels")->check(CLI::PositiveNumber);
	app.add_option("-p,--period-size", settings.m_periodSize, "Period size")->check(CLI::PositiveNumber);
	app.add_option("--periods", settings.m_periods, "Number of periods")->check(CLI::PositiveNumber);
	app.add_flag("--dump-correlation", settings.m_dumpCorrelation, "Enable correlation data dumping");
	app.add_option("--block-size", settings.m_blockSize, "FFT block size")->check(CLI::PositiveNumber);
	app.add_option("--overlap", settings.m_overlapPercent, "Overlap percent")->check(CLI::IsMember({ 50, 75 }));
	app.add_option("--bands", settings.m_numBands, "Number of frequency bands")
		->check(CLI::Range(SyncCheckConfig::MIN_BANDS, SyncCheckConfig::MAX_BANDS));
	app.add_option("--interp", settings.m_interpFactor, "Interpolation factor")->check(CLI::PositiveNumber);
	app.add_option("--start-freq", settings.m_startFreq, "Start frequency in Hz");
	app.add_option("--freq-range", settings.m_freqRange, "Frequency range in Hz")->check(CLI::PositiveNumber);
	app.add_option("--band-sep", settings.m_bandSep, "Band separation in Hz");
	app.add_option("--refresh-rate", settings.m_statsRefreshRate, "Statistics refresh rate in seconds")->check(CLI::PositiveNumber);
	app.add_flag("--dump", settings.m_dumpAudio, "Enable audio dump to WAV files (auto-named)");
	app.add_option("-S,--sources", settings.m_sources,
		"Input mapping. Use \"mix\" to mix all channels or provide a comma-separated list (e.g. \"0:0,1:1\" or \"0,1\")");
	app.add_flag("--realtime", settings.m_realtimeFile, "Analyze files at real playback speed");
	app.add_flag("-l,--list-devices", listDevicesOnly, "List audio devices and exit");

	CLI11_PARSE(app, argc, argv);

	// Set desired log level
	Logger::getInstance().setLogLevel(LOG_DEBUG);

	if (listDevicesOnly) {
		listDevices();
		return 0;
	}

	if (!noColors) {
		enableAnsiColors();
	}

	// Set up signal handling
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	// Initialize visualizer
	DelayStatsVisualizer visualizer(dumpStatsFileName, !noColors);

	std::string error;
	const bool ok = g_analyzer.run(settings, [&](const SyncAnalyzerSnapshot &snapshot) {
		std::vector<FFTSyncCheck::DelayStatsReport> estimates;
		for (const auto &result : snapshot.m_results) {
			estimates.push_back(result.m_report);
		}
		visualizer.visualizeDelays(estimates, snapshot.m_timestamp);
	}, error);

	if (!ok) {
		DBG_ERROR(error);
		return 1;
	}
	return 0;
}
