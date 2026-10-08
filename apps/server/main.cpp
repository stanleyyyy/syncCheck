#include "AnalyzerService.h"
#include "AudioDevice.h"
#include "CLI11.hpp"
#include "Logger.h"
#include "NoiseSignalGenerator.h"
#include "TestSignalGenerator.h"
#include "WavFile.h"
#include "WebAssets.h"
#include "httplib.h"
#include <atomic>
#include <csignal>
#include <thread>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using nlohmann::json;
namespace fs = std::filesystem;

static std::atomic<bool> g_quit { false };

static void signalHandler(int)
{
	g_quit = true;
}

static std::string defaultDataDir()
{
#ifdef _WIN32
	if (const char *dir = std::getenv("LOCALAPPDATA")) {
		return (fs::path(dir) / "syncCheck").string();
	}
#else
	if (const char *dir = std::getenv("XDG_DATA_HOME")) {
		return (fs::path(dir) / "syncCheck").string();
	}
	if (const char *home = std::getenv("HOME")) {
		return (fs::path(home) / ".local" / "share" / "syncCheck").string();
	}
#endif
	return "syncCheck-data";
}

static bool haveDisplay()
{
#if defined(_WIN32) || defined(__APPLE__)
	return true;
#else
	return std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY");
#endif
}

static void openBrowser(const std::string &url)
{
#if defined(_WIN32)
	const std::string command = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
	const std::string command = "open \"" + url + "\"";
#else
	const std::string command = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
#endif
	if (std::system(command.c_str()) != 0) {
		DBG_WARN("Could not open a browser, open " << url << " manually");
	}
}

static const char *mimeType(const std::string &path)
{
	const std::string ext = fs::path(path).extension().string();
	if (ext == ".html") return "text/html; charset=utf-8";
	if (ext == ".js") return "text/javascript; charset=utf-8";
	if (ext == ".css") return "text/css; charset=utf-8";
	if (ext == ".svg") return "image/svg+xml";
	if (ext == ".png") return "image/png";
	return "application/octet-stream";
}

static void sendJson(httplib::Response &res, const json &body, int status = 200)
{
	res.status = status;
	res.set_content(body.dump(), "application/json");
}

static void sendError(httplib::Response &res, const std::string &message, int status = 400)
{
	sendJson(res, { { "error", message } }, status);
}

static json fileInfo(const std::string &path)
{
	WavReader reader;
	if (!reader.open(path)) {
		return nullptr;
	}
	const double duration = reader.getSampleRate() ? double(reader.getLengthInFrames()) / reader.getSampleRate() : 0.0;
	return { { "path", path }, { "channels", reader.getChannels() }, { "sampleRate", reader.getSampleRate() }, { "duration", duration } };
}

static double queryNumber(const httplib::Request &req, const char *name, double fallback)
{
	if (!req.has_param(name)) {
		return fallback;
	}
	try {
		return std::stod(req.get_param_value(name));
	} catch (const std::exception &) {
		return fallback;
	}
}

static std::vector<float> parseList(const std::string &text)
{
	std::vector<float> values;
	std::stringstream ss(text);
	std::string item;
	while (std::getline(ss, item, ',')) {
		if (!item.empty()) {
			values.push_back(std::stof(item));
		}
	}
	return values;
}

/**
 * @brief Render a test signal into a WAV file in memory.
 */
static bool generateSignal(const httplib::Request &req, const ServerSettings &settings, const std::string &tempDir,
	std::string &wav, std::string &name, std::string &error)
{
	const std::string mode = req.has_param("mode") ? req.get_param_value("mode") : "channels";
	const float duration = static_cast<float>(std::clamp(queryNumber(req, "duration", 60.0), 1.0, 3600.0));
	const unsigned int sampleRate = static_cast<unsigned int>(queryNumber(req, "sampleRate", settings.m_analyzer.m_sampleRate));
	std::vector<float> delays;
	try {
		delays = parseList(req.get_param_value("delays"));
	} catch (const std::exception &) {
		error = "invalid delays";
		return false;
	}

	std::vector<float> signal;
	unsigned int channels = 1;
	if (mode == "bands") {
		// Band test signal with the current band settings, as sync_generator does.
		const SyncAnalyzerSettings &a = settings.m_analyzer;
		const unsigned int bands = static_cast<unsigned int>(queryNumber(req, "bands", a.m_numBands));
		if (bands < 2 || bands > (unsigned int)SyncCheckConfig::MAX_BANDS) {
			error = "band mode needs 2 to 8 bands";
			return false;
		}
		delays.resize(bands, 0.0f);
		SyncCheckConfig config(a.m_blockSize, a.m_overlapPercent, sampleRate, bands, a.m_interpFactor);
		config.initBands(a.m_startFreq, a.m_freqRange, a.m_bandSep);
		TestSignalGenerator generator(config);
		signal = generator.renderTestSignal(duration, delays);
		name = "sync_test_bands.wav";
	} else {
		channels = static_cast<unsigned int>(std::clamp(queryNumber(req, "channels", 2), 2.0, 64.0));
		signal = generateNoiseTestSignal(channels, sampleRate, duration, delays);
		name = "sync_test_noise_" + std::to_string(channels) + "ch.wav";
	}

	const std::string path = (fs::path(tempDir) / name).string();
	{
		WavWriter writer;
		if (!writer.open(path, channels, sampleRate)) {
			error = "cannot write " + path;
			return false;
		}
		writer.writeFrames(signal.data(), signal.size() / channels);
	}
	std::ifstream file(path, std::ios::binary);
	wav.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	file.close();
	std::error_code ec;
	fs::remove(path, ec);
	return true;
}

/**
 * @brief Cut a part of an audio file into a WAV file in memory.
 * @param channelList Comma separated channels to keep, empty keeps all.
 */
static bool extractAudio(const std::string &path, double from, double to, const std::string &channelList,
	const std::string &tempDir, std::string &wav, std::string &error)
{
	WavReader reader;
	if (path.empty() || !reader.open(path)) {
		error = "this session has no audio";
		return false;
	}
	const unsigned int channels = reader.getChannels();
	const unsigned int sampleRate = reader.getSampleRate();

	std::vector<unsigned int> keep;
	try {
		for (float ch : parseList(channelList)) {
			if (ch < 0 || ch >= channels) {
				error = "invalid channel";
				return false;
			}
			keep.push_back(static_cast<unsigned int>(ch));
		}
	} catch (const std::exception &) {
		error = "invalid channel list";
		return false;
	}
	if (keep.empty()) {
		for (unsigned int ch = 0; ch < channels; ch++) {
			keep.push_back(ch);
		}
	}

	// At most 10 minutes per request.
	const std::size_t length = reader.getLengthInFrames();
	const std::size_t start = std::min<std::size_t>(static_cast<std::size_t>(std::max(0.0, from) * sampleRate), length);
	std::size_t end = std::min<std::size_t>(static_cast<std::size_t>(std::max(from, to) * sampleRate), length);
	end = std::min<std::size_t>(end, start + std::size_t(600) * sampleRate);
	if (end <= start || !reader.seek(start)) {
		error = "empty range";
		return false;
	}

	std::vector<float> input((end - start) * channels);
	const std::size_t frames = reader.readFrames(input.data(), end - start);
	std::vector<float> output(frames * keep.size());
	for (std::size_t i = 0; i < frames; i++) {
		for (std::size_t c = 0; c < keep.size(); c++) {
			output[i * keep.size() + c] = input[i * channels + keep[c]];
		}
	}

	static std::atomic<unsigned int> counter { 0 };
	const std::string temp = (fs::path(tempDir) / ("playback-" + std::to_string(counter++) + ".wav")).string();
	{
		WavWriter writer;
		if (!writer.open(temp, static_cast<unsigned int>(keep.size()), sampleRate)) {
			error = "cannot write " + temp;
			return false;
		}
		writer.writeFrames(output.data(), frames);
	}
	std::ifstream file(temp, std::ios::binary);
	wav.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	file.close();
	std::error_code ec;
	fs::remove(temp, ec);
	return true;
}

int main(int argc, char *argv[])
{
	std::string host = "127.0.0.1";
	int port = 8090;
	std::string dataDir = defaultDataDir();
	std::string webRoot;
	bool noBrowser = false;

	CLI::App app { "Sync analyzer server: measure audio synchronization in a web browser." };
	app.option_defaults()->always_capture_default();
	app.add_option("--host", host, "Address to listen on; use 0.0.0.0 to allow access from other machines");
	app.add_option("-p,--port", port, "Port to listen on")->check(CLI::Range(1, 65535));
	app.add_option("--data-dir", dataDir, "Directory for settings, sessions and uploads");
	app.add_option("--web-root", webRoot, "Serve the web UI from this directory instead of the built-in copy");
	app.add_flag("--no-browser", noBrowser, "Do not open a browser");
	CLI11_PARSE(app, argc, argv);

	Logger::getInstance().setLogLevel(LOG_INFO);

	AnalyzerService service(dataDir);
	const std::string tempDir = (fs::path(dataDir) / "tmp").string();
	const std::string uploadDir = (fs::path(dataDir) / "uploads").string();
	std::error_code ec;
	fs::create_directories(tempDir, ec);
	fs::create_directories(uploadDir, ec);

	httplib::Server server;

	server.Get("/api/state", [&](const httplib::Request &, httplib::Response &res) { sendJson(res, service.state()); });

	server.Get("/api/devices", [&](const httplib::Request &, httplib::Response &res) {
		auto list = [](AudioDevices::Direction direction) {
			json devices = json::array();
			for (const auto &device : AudioDevices::list(direction)) {
				devices.push_back({ { "name", device.m_name }, { "backend", device.m_backend },
					{ "isDefault", device.m_isDefault }, { "channels", device.m_maxChannels } });
			}
			return devices;
		};
		sendJson(res, { { "capture", list(AudioDevices::Direction::Capture) }, { "playback", list(AudioDevices::Direction::Playback) } });
	});

	server.Get("/api/settings", [&](const httplib::Request &, httplib::Response &res) { sendJson(res, service.settings()); });

	server.Put("/api/settings", [&](const httplib::Request &req, httplib::Response &res) {
		try {
			ServerSettings settings = service.settings();
			from_json(json::parse(req.body), settings);
			std::string error;
			if (!SyncAnalyzer::validate(settings.m_analyzer, error)) {
				sendError(res, error);
				return;
			}
			service.setSettings(settings);
			sendJson(res, settings);
		} catch (const std::exception &e) {
			sendError(res, std::string("invalid settings: ") + e.what());
		}
	});

	server.Post("/api/settings/reset", [&](const httplib::Request &, httplib::Response &res) {
		service.resetSettings();
		sendJson(res, service.settings());
	});

	// Audio of the session between from and to (seconds), optionally only some channels, for playback.
	server.Get("/api/audio.wav", [&](const httplib::Request &req, httplib::Response &res) {
		std::string wav, error;
		if (!extractAudio(service.sessionAudio(), queryNumber(req, "from", 0), queryNumber(req, "to", 0),
				req.get_param_value("channels"), tempDir, wav, error)) {
			sendError(res, error, 404);
			return;
		}
		res.set_content(std::move(wav), "audio/wav");
	});

	server.Post("/api/start", [&](const httplib::Request &, httplib::Response &res) {
		std::string error;
		if (!service.start(error)) {
			sendError(res, error);
			return;
		}
		sendJson(res, service.state());
	});

	server.Post("/api/stop", [&](const httplib::Request &, httplib::Response &res) {
		service.stop();
		sendJson(res, service.state());
	});

	server.Get("/api/measurements", [&](const httplib::Request &req, httplib::Response &res) {
		sendJson(res, service.measurements(static_cast<std::size_t>(queryNumber(req, "from", 0))));
	});

	// Exports, of the current session or of a stored one (key); file names start with the session identifier.
	auto sendCsv = [&](const std::string &key, httplib::Response &res) {
		std::string csv, filename, error;
		if (!service.sessionCsv(key, csv, filename, error)) {
			sendError(res, error, 404);
			return;
		}
		res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
		res.set_content(std::move(csv), "text/csv");
	};
	auto sendAudio = [&](const std::string &key, httplib::Response &res) {
		std::string path, filename, error;
		if (!service.sessionAudioFile(key, path, filename, error)) {
			sendError(res, error, 404);
			return;
		}
		res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
		res.set_file_content(path, "application/octet-stream");
	};
	server.Get("/api/measurements.csv", [&](const httplib::Request &, httplib::Response &res) { sendCsv("", res); });
	server.Get("/api/recording.wav", [&](const httplib::Request &, httplib::Response &res) { sendAudio("", res); });
	server.Get(R"(/api/sessions/([\w-]+)/measurements\.csv)", [&](const httplib::Request &req, httplib::Response &res) {
		sendCsv(req.matches[1], res);
	});
	server.Get(R"(/api/sessions/([\w-]+)/audio)", [&](const httplib::Request &req, httplib::Response &res) {
		sendAudio(req.matches[1], res);
	});

	// Session library
	server.Get("/api/sessions", [&](const httplib::Request &, httplib::Response &res) {
		sendJson(res, { { "sessions", service.listSessions() }, { "disk", service.disk() } });
	});
	server.Post(R"(/api/sessions/([\w-]+)/open)", [&](const httplib::Request &req, httplib::Response &res) {
		std::string error;
		if (!service.openSession(req.matches[1], error)) {
			sendError(res, error);
			return;
		}
		sendJson(res, service.state());
	});
	server.Put(R"(/api/sessions/([\w-]+))", [&](const httplib::Request &req, httplib::Response &res) {
		std::string error;
		try {
			if (!service.updateSession(req.matches[1], json::parse(req.body), error)) {
				sendError(res, error);
				return;
			}
		} catch (const std::exception &e) {
			sendError(res, e.what());
			return;
		}
		sendJson(res, { { "ok", true } });
	});
	server.Delete(R"(/api/sessions/([\w-]+))", [&](const httplib::Request &req, httplib::Response &res) {
		std::string error;
		if (!service.deleteSession(req.matches[1], error)) {
			sendError(res, error);
			return;
		}
		sendJson(res, { { "ok", true } });
	});
	server.Get("/api/disk", [&](const httplib::Request &, httplib::Response &res) { sendJson(res, service.disk()); });

	server.Get("/api/inspect", [&](const httplib::Request &req, httplib::Response &res) {
		json result;
		std::string error;
		const bool live = req.has_param("live") && req.get_param_value("live") == "1";
		if (!service.inspect(queryNumber(req, "t", 0), std::clamp(queryNumber(req, "range", 10), 0.1, 2000.0),
				std::clamp(queryNumber(req, "window", 20), 0.5, 60000.0), queryNumber(req, "offset", 0), live,
				req.get_param_value("align") == "1", result, error)) {
			sendError(res, error, 404);
			return;
		}
		sendJson(res, result);
	});


	server.Get("/api/fileinfo", [&](const httplib::Request &req, httplib::Response &res) {
		const json info = fileInfo(req.get_param_value("path"));
		if (info.is_null()) {
			sendError(res, "cannot open " + req.get_param_value("path"), 404);
			return;
		}
		sendJson(res, info);
	});

	server.Post("/api/upload", [&](const httplib::Request &req, httplib::Response &res) {
		if (!req.has_file("file")) {
			sendError(res, "no file in the request");
			return;
		}
		const auto file = req.get_file_value("file");
		const std::string name = fs::path(file.filename).filename().string();
		if (name.empty()) {
			sendError(res, "invalid file name");
			return;
		}
		const std::string path = (fs::path(uploadDir) / name).string();
		std::ofstream(path, std::ios::binary) << file.content;
		const json info = fileInfo(path);
		if (info.is_null()) {
			fs::remove(path, ec);
			sendError(res, "not a supported audio file: " + name);
			return;
		}
		sendJson(res, info);
	});

	server.Get("/api/generate", [&](const httplib::Request &req, httplib::Response &res) {
		std::string wav, name, error;
		if (!generateSignal(req, service.settings(), tempDir, wav, name, error)) {
			sendError(res, error);
			return;
		}
		res.set_header("Content-Disposition", "attachment; filename=\"" + name + "\"");
		res.set_content(std::move(wav), "audio/wav");
	});

	server.Get("/api/events", [&](const httplib::Request &req, httplib::Response &res) {
		auto cursor = std::make_shared<AnalyzerService::Cursor>();
		// Resume after a reconnect: Last-Event-ID is "session:row".
		const std::string lastId = req.get_header_value("Last-Event-ID");
		const std::size_t colon = lastId.find(':');
		if (colon != std::string::npos) {
			try {
				cursor->m_session = std::stoi(lastId.substr(0, colon));
				cursor->m_row = std::stoul(lastId.substr(colon + 1));
			} catch (const std::exception &) {
				*cursor = AnalyzerService::Cursor();
			}
		}
		res.set_header("Cache-Control", "no-cache");
		res.set_chunked_content_provider("text/event-stream", [&service, cursor](std::size_t, httplib::DataSink &sink) {
			if (service.isShutDown()) {
				return false;
			}
			std::string events = service.eventsSince(*cursor);
			if (events.empty()) {
				service.waitForChange(*cursor, std::chrono::seconds(15));
				events = service.eventsSince(*cursor);
			}
			if (events.empty()) {
				events = ": keepalive\n\n";
			}
			return sink.write(events.data(), events.size());
		});
	});

	// Web UI: embedded files, or files from --web-root for development.
	server.Get(R"(/(.*))", [&](const httplib::Request &req, httplib::Response &res) {
		std::string path = req.path == "/" ? "/index.html" : req.path;
		if (path.find("..") != std::string::npos) {
			res.status = 404;
			return;
		}
		if (!webRoot.empty()) {
			std::ifstream file(fs::path(webRoot) / path.substr(1), std::ios::binary);
			if (file) {
				res.set_content(std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()), mimeType(path));
				return;
			}
		} else if (const WebAsset *asset = findWebAsset(path)) {
			res.set_content(reinterpret_cast<const char *>(asset->m_data), asset->m_size, mimeType(path));
			return;
		}
		res.status = 404;
	});

	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	if (!server.bind_to_port(host, port)) {
		DBG_ERROR("Cannot listen on " << host << ":" << port);
		return 1;
	}

	const std::string url = "http://" + std::string(host == "0.0.0.0" ? "127.0.0.1" : host) + ":" + std::to_string(port) + "/";
	DBG_INFO("Sync analyzer running at " << url << " (data in " << dataDir << "), press Ctrl+C to quit");
	if (host == "127.0.0.1" || host == "localhost") {
		DBG_INFO("Only reachable from this machine; use --host 0.0.0.0 to allow access from the network");
	}
	if (!noBrowser && haveDisplay()) {
		openBrowser(url);
	}

	// Stop the service first so that open event streams return, then the server.
	std::thread quitWatcher([&] {
		while (!g_quit) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		service.shutdown();
		server.stop();
	});
	server.listen_after_bind();
	g_quit = true;
	quitWatcher.join();
	return 0;
}
