set(syncCheck_PUBLIC_HEADERS
	BandProcessor.h
	SyncCheck.h
	SyncCheckConfig.h
	TestSignalGenerator.h
)

nsdk_package_create(
	syncCheck
	MANDATORY
	TARGETS
		nsdk_syncCheck SHARED libnsdk_syncCheck.so
		asound_module_pcm_bandmover
	FILES
		${syncCheck_PUBLIC_HEADERS}
)

# This is A MUST in order to build the alsa pcm plugin!
add_definitions(-DPIC)

target_link_libraries(nsdk_syncCheck PUBLIC m sndfile)
target_link_libraries(asound_module_pcm_bandmover PUBLIC
	asound
	dl
	nsdk_syncCheck
)

target_link_libraries(nsdk_syncCheck_test nsdk_syncCheck)

target_include_directories(nsdk_syncCheck PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Install targets
install(TARGETS nsdk_syncCheck DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS asound_module_pcm_bandmover DESTINATION ${CMAKE_INSTALL_LIBDIR}/alsa-lib)
