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

target_link_libraries(nsdk_syncCheck PUBLIC
	m
	sndfile
)

target_link_libraries(asound_module_pcm_bandmover PRIVATE
	nsdk_syncCheck
	asound
	dl
)

target_link_libraries(alsa_sync_check PUBLIC
	${QtCore}
	nsdk_api
	nsdk_syncCheck
	asound
)

target_link_libraries(nsdk_syncCheck_test PUBLIC
	nsdk_syncCheck
)

# Install targets
install(TARGETS nsdk_syncCheck DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS asound_module_pcm_bandmover DESTINATION ${CMAKE_INSTALL_LIBDIR}/alsa-lib)
install(TARGETS alsa_sync_check DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS nsdk_syncCheck_test DESTINATION ${CMAKE_INSTALL_BINDIR})
