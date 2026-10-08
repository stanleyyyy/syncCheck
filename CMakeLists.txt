cmake_minimum_required(VERSION 3.16)

project(syncCheck VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

option(SYNCCHECK_BUILD_TOOLS "Build command line tools" ON)
option(SYNCCHECK_BUILD_TESTS "Build test programs" ON)
option(SYNCCHECK_BUILD_ALSA_PLUGIN "Build the ALSA bandmover plugin (Linux only)" OFF)


set(syncCheck_PUBLIC_HEADERS
	dsp/BandProcessor.h
	dsp/SyncCheck.h
	dsp/SyncCheckConfig.h
	dsp/TestSignalGenerator.h
	kissfft/kiss_fftr.h
)

set(syncCheck_HEADERS
	${syncCheck_PUBLIC_HEADERS}
	dsp/ButterworthLowpass.h
	dsp/DelayLine.h
	dsp/SyncSignalGenerator.h
	dsp/STFTProcessor.h
	dsp/STFTWindowGenerator.h
	dsp/KissFFTWrapper.h
	dsp/KissFFTComplexWrapper.h
	dsp/HilbertShifter.h
	utils/Logger.h
	utils/AudioDevice.h
	utils/AudioDeviceImpl.h
	utils/AudioInput.h
	utils/AudioOutput.h
	utils/DelayStatsVisualizer.h
	analyzer/SyncAnalyzer.h
	utils/WavFile.h
)

set(syncCheck_SOURCES
	dsp/BandProcessor.cpp
	dsp/DelayLine.cpp
	dsp/SyncCheck.cpp
	dsp/SyncCheckConfig.cpp
	dsp/TestSignalGenerator.cpp
	dsp/SyncSignalGenerator.cpp
	dsp/STFTProcessor.cpp
	dsp/STFTWindowGenerator.cpp
	dsp/KissFFTWrapper.cpp
	dsp/KissFFTComplexWrapper.cpp
	dsp/HilbertShifter.cpp
	utils/Logger.cpp
	utils/WavFile.cpp
	utils/AudioDevice.cpp
	utils/AudioInput.cpp
	utils/AudioOutput.cpp
	analyzer/SyncAnalyzer.cpp
	utils/MiniaudioImpl.c
	kissfft/kiss_fft.c
	kissfft/kiss_fftr.c
)

add_library(synccheck ${syncCheck_SOURCES} ${syncCheck_HEADERS})
set_target_properties(synccheck PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	VERSION ${PROJECT_VERSION}
	SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_include_directories(synccheck PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/dsp
	${CMAKE_CURRENT_SOURCE_DIR}/utils
	${CMAKE_CURRENT_SOURCE_DIR}/kissfft
	${CMAKE_CURRENT_SOURCE_DIR}/analyzer
)
target_include_directories(synccheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio)
find_package(Threads REQUIRED)
target_link_libraries(synccheck PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(NOT WIN32)
	target_link_libraries(synccheck PUBLIC m)
endif()
if(WIN32)
	target_compile_definitions(synccheck PUBLIC _USE_MATH_DEFINES)
endif()
if(NOT MSVC)
	target_compile_options(synccheck PRIVATE -O3 -ftree-vectorize)
endif()

install(TARGETS synccheck
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(SYNCCHECK_BUILD_TOOLS)
	add_executable(sync_analyzer apps/cli/syncAnalyzer.cpp)
	target_link_libraries(sync_analyzer PRIVATE synccheck)
	target_include_directories(sync_analyzer PRIVATE third_party/CLI11)

	add_executable(sync_generator apps/cli/syncGenerator.cpp)
	target_link_libraries(sync_generator PRIVATE synccheck)
	target_include_directories(sync_generator PRIVATE third_party/CLI11)

	install(TARGETS sync_analyzer sync_generator DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(SYNCCHECK_BUILD_TESTS)
	add_executable(sync_generator_analyzer_test test/syncGeneratorAnalyzerTest.cpp)
	target_link_libraries(sync_generator_analyzer_test PRIVATE synccheck)

	add_executable(sync_generator_test test/syncGeneratorTest.cpp)
	target_link_libraries(sync_generator_test PRIVATE synccheck)

	add_executable(sync_generator_window_test test/syncGeneratorWindowTest.cpp)
	target_link_libraries(sync_generator_window_test PRIVATE synccheck)

	add_executable(hilbert_test test/hilbertTest.cpp)
	target_link_libraries(hilbert_test PRIVATE synccheck)
	target_include_directories(hilbert_test PRIVATE third_party/CLI11)
endif()

if(SYNCCHECK_BUILD_ALSA_PLUGIN)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
	add_library(asound_module_pcm_bandmover MODULE alsa-plugin/pcm_bandmover.cpp)
	target_compile_definitions(asound_module_pcm_bandmover PRIVATE PIC)
	target_link_libraries(asound_module_pcm_bandmover PRIVATE synccheck PkgConfig::ALSA ${CMAKE_DL_LIBS})
	set_target_properties(asound_module_pcm_bandmover PROPERTIES PREFIX "lib")
	install(TARGETS asound_module_pcm_bandmover DESTINATION ${CMAKE_INSTALL_LIBDIR}/alsa-lib)
endif()
