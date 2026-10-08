# syncCheck

Measures audio synchronization by DSP. A test signal carries several narrow
frequency bands; the analyzer captures it (from a sound card or a file) and
measures the delay of every band or channel against a reference band using
FFT based cross correlation.

Contents:

| Component | Description |
|-----------|-------------|
| `libsynccheck` | DSP library (`dsp/`, `kissfft/`), audio and WAV I/O (`utils/`), analyzer engine (`analyzer/`) |
| `sync_analyzer` | Command line analyzer |
| `sync_generator` | Command line test signal generator |
| `libasound_module_pcm_bandmover.so` | Optional ALSA plugin (Linux only) |
| `tools/*.py` | Visualization of correlation and statistics dumps |

Audio I/O uses [miniaudio](https://miniaud.io): ALSA, PulseAudio/PipeWire and
JACK on Linux, WASAPI/DirectSound on Windows, Core Audio on macOS.

## Building

Requirements: CMake 3.16+ and a C++17 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build
```

Options:

| Option | Default | |
|--------|---------|-|
| `SYNCCHECK_BUILD_TOOLS` | ON | Command line tools |
| `SYNCCHECK_BUILD_TESTS` | ON | Tests |
| `SYNCCHECK_BUILD_ALSA_PLUGIN` | OFF | ALSA bandmover plugin (needs `libasound2-dev`) |

The library and command line tools have no dependencies besides the C++
runtime, which makes them suitable for embedded/Yocto builds.

## Usage

```sh
# Generate a test signal (8 bands, 16-24 kHz) and analyze it
sync_generator -d 60 -o test
sync_analyzer -d file://test_mixed.wav --bands 8 --interp 1

# List audio devices, then measure live from a sound card
sync_analyzer --list-devices
sync_analyzer -d "USB Audio" -c 4 -S 0:0,1:0,2:0,3:0 -o stats.csv
```

`-d/--device` accepts `default`, a device name (or a case insensitive part
of it), `BACKEND:NAME` (e.g. `pulseaudio:default`, `wasapi:Microphone`),
`alsa:PCM` for raw ALSA PCM names (e.g. `alsa:hw:1,0` or a name from
`asound.conf`) and `file://PATH` for WAV/FLAC/MP3 files.

`-S/--sources` selects what is compared: `mix` mixes all channels and compares
every band against band 0; a list of `channel:band` pairs compares every pair
against the first one.

## Packaging

```sh
cmake --build build --target package    # Linux: .tar.gz, .deb, .rpm (if rpmbuild is installed)
                                        # Windows: .zip, NSIS installer (if makensis is installed)
```

Installers contain the applications. The library, headers and CMake package
(`find_package(synccheck)`, target `synccheck::synccheck`) are installed with
`cmake --install build --component development`.

## Third party code

miniaudio (Unlicense / MIT-0), CLI11 (BSD-3-Clause) and kissfft
(BSD-3-Clause) are vendored in `third_party/` and `kissfft/`. Their
licenses are installed to `share/doc/synccheck/licenses`.
