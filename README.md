# syncCheck

Measures audio synchronization by DSP. The analyzer captures audio (from a
sound card or a file) and measures, using FFT based cross correlation, the
delay of every input channel against a reference channel, or of the frequency
bands of a band test signal against band 0.

Contents:

| Component | Description |
|-----------|-------------|
| `libsynccheck` | DSP library (`dsp/`, `kissfft/`), audio and WAV I/O (`utils/`), analyzer engine (`analyzer/`) |
| `sync_analyzer_server` | Web UI: live measurement, session history, inspection and playback, in any browser |
| `sync_analyzer` | Command line analyzer |
| `sync_generator` | Command line test signal generator |
| `libasound_module_pcm_bandmover.so` | Optional ALSA plugin (Linux only) |
| `tools/*.py` | Visualization of correlation and statistics dumps |

Audio I/O uses [miniaudio](https://miniaud.io): ALSA, PulseAudio/PipeWire and
JACK on Linux, WASAPI/DirectSound on Windows, Core Audio on macOS.

## Installing

| Platform | Package | |
|----------|---------|-|
| Debian, Ubuntu | `synccheck_<version>_amd64.deb` | `sudo apt install ./synccheck_*.deb` |
| Fedora, openSUSE | `synccheck-<version>.x86_64.rpm` | `sudo dnf install ./synccheck-*.rpm` |
| Any Linux | `Sync-Analyzer-x86_64.AppImage` | `chmod +x` and run it |
| Any Linux | `synccheck-<version>-Linux.tar.gz` | unpack, run `bin/sync_analyzer_server` |
| Windows | `synccheck-<version>-win64.exe` / `.zip` | installer, or unpack the zip |

## Running the web UI

**As an app:** start *Sync Analyzer* from the application menu (or run
`sync_analyzer_server --app`); on Windows see the tray app below. It opens the UI in the browser and quits a
minute after the last browser tab is closed, unless a measurement is running.
Starting it again while it runs just opens the browser.

**As a service** (always running, reachable from other devices such as
phones): the Linux packages include a systemd user service.

```sh
systemctl --user enable --now sync-analyzer    # start now and after every login
sudo loginctl enable-linger $USER              # optional: also without being logged in
```

It listens on port 8090 of all network interfaces: open
`http://<computer>:8090/`. Allow the port in the firewall, e.g.
`sudo ufw allow 8090/tcp`. Opening the app from the menu then shows the
running service.

**On Windows** the Start menu entry *Sync Analyzer* starts the tray app
(`sync_analyzer_tray.exe`): the server runs behind an icon in the
notification area and the UI opens in the browser. Double-click the icon to
open the UI again; its menu has *Start with Windows* (runs it at every login,
without opening the browser) and *Quit*. Like the Linux service it listens on
port 8090 of all network interfaces; allow it when Windows asks about the
firewall to reach it from other devices.

**By hand:** `sync_analyzer_server [--host 0.0.0.0] [--port 8090]`. Without
`--host` it is only reachable from the local machine.

The server has no access control: whoever reaches it can start measurements,
read audio files by path on that computer and delete sessions. Only open it
to networks you trust.

Settings and sessions (measurements, recordings) are stored in
`~/.local/share/syncCheck` (Linux) or `%LOCALAPPDATA%\syncCheck` (Windows),
or in `--data-dir`.

## Command line

```sh
# Generate a test signal with known delays and analyze it
sync_generator -m channels -c 4 --delays 0,1,-2.5,0.375 -d 60 -o test.wav
sync_analyzer -d file://test.wav -m channels

# List audio devices, then measure live from a sound card
sync_analyzer --list-devices
sync_analyzer -d "USB Audio" -c 4 -m channels -o stats.csv
```

`-d/--device` accepts `default`, a device name (or a case insensitive part
of it), `BACKEND:NAME` (e.g. `pulseaudio:default`, `wasapi:Microphone`),
`alsa:PCM` for raw ALSA PCM names (e.g. `alsa:hw:1,0` or a name from
`asound.conf`) and `file://PATH` for WAV/FLAC/MP3 files.

`-m channels` compares `--reference` with the other channels over
`--min-freq`..`--max-freq`. The default mode uses `-S/--sources`: `mix` mixes
all channels and compares every band of a band test signal
(`sync_generator -m bands`) against band 0; a list of `channel:band` pairs
compares every pair against the first one.

## Building

Requirements: CMake 3.16+ and a C++17 compiler. All dependencies are
vendored in `third_party/`, so no network access is needed.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build
```

Options:

| Option | Default | |
|--------|---------|-|
| `SYNCCHECK_BUILD_TOOLS` | ON | Command line tools |
| `SYNCCHECK_BUILD_SERVER` | ON | Web UI server |
| `SYNCCHECK_BUILD_TESTS` | ON | Tests |
| `SYNCCHECK_BUILD_ALSA_PLUGIN` | OFF | ALSA bandmover plugin (needs `libasound2-dev`) |

During web UI development, `sync_analyzer_server --web-root apps/server/web`
serves the files from disk instead of the copy built into the binary.

## Packaging

```sh
cmake --build build --target package    # Linux: .tar.gz, .deb, .rpm (if rpmbuild is installed)
                                        # Windows: .zip, NSIS installer (if makensis is installed)
packaging/appimage/build-appimage.sh    # Linux: portable AppImage
```

Build packages on the oldest distribution you want to support; glibc is not
bundled.

Releases are built by GitHub Actions (`.github/workflows/release.yml`): set the
version in `CMakeLists.txt`, commit, then push a matching tag, e.g.
`git tag v0.2.0 && git push origin v0.2.0`. The workflow builds and tests on
Ubuntu 22.04 and Windows and publishes the `.deb`, `.tar.gz`, AppImage, `.zip`
and installer as a GitHub release. Running the workflow by hand (Actions tab)
only builds the packages and keeps them as artifacts. Installers contain the applications. The library, headers and CMake
package (`find_package(synccheck)`, target `synccheck::synccheck`) are
installed with `cmake --install build --component development`.

## Third party code

miniaudio (Unlicense / MIT-0), CLI11 (BSD-3-Clause), cpp-httplib (MIT),
nlohmann/json (MIT), uPlot (MIT) and kissfft (BSD-3-Clause) are vendored.
Their licenses are installed to `share/doc/synccheck/licenses`.
