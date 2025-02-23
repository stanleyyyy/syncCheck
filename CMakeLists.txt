set(syncCheck_PUBLIC_HEADERS
	dsp/BandProcessor.h
	dsp/SyncCheck.h
	dsp/SyncCheckConfig.h
	dsp/TestSignalGenerator.h
	utils/AudioSyncSharedTypes.h
	kissfft/kiss_fftr.h
)

if(BUILDTYPE_USE_SYNC_ANALYZER)
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
		nsdk_simple_ipc
	)

	target_link_libraries(asound_module_pcm_bandmover PRIVATE
		nsdk_syncCheck
		asound
		dl
	)

	target_link_libraries(nsdk_sync_analyzer_tool PUBLIC
		nsdk_syncCheck
		nsdk_api
		asound
	)

	target_link_libraries(nsdk_sync_generator PUBLIC
		nsdk_syncCheck
		nsdk_api
	)

	target_link_libraries(nsdk_sync_generator_analyzer_test PUBLIC
		nsdk_syncCheck
	)

	target_link_libraries(nsdk_sync_generator_test PUBLIC
		nsdk_syncCheck
	)

	target_link_libraries(nsdk_sync_generator_window_test PUBLIC
		nsdk_syncCheck
	)

	target_link_libraries(nsdk_hilbert_test PUBLIC
		nsdk_syncCheck
	)

	target_include_directories(nsdk_syncCheck PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
	target_include_directories(nsdk_syncCheck PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dsp)
	target_include_directories(nsdk_syncCheck PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/utils)
	target_include_directories(nsdk_syncCheck PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/kissfft)

	# Install targets
	install(TARGETS nsdk_syncCheck DESTINATION ${CMAKE_INSTALL_LIBDIR})
	install(TARGETS asound_module_pcm_bandmover DESTINATION ${CMAKE_INSTALL_LIBDIR}/alsa-lib)

	# Install main executables
	install(TARGETS nsdk_sync_generator DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS nsdk_sync_analyzer_tool DESTINATION ${CMAKE_INSTALL_BINDIR})

	# Install test executables
	install(TARGETS nsdk_sync_generator_analyzer_test DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS nsdk_sync_generator_test DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS nsdk_sync_generator_window_test DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(TARGETS nsdk_hilbert_test DESTINATION ${CMAKE_INSTALL_BINDIR})

else()
	nsdk_package_create(
		syncCheck
		DUMMY
		FILES
			${syncCheck_PUBLIC_HEADERS}
	)
endif()
