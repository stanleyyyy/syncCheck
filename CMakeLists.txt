set(syncCheck_PUBLIC_HEADERS
	SyncCheck.h
)

nsdk_package_create(
	syncCheck
	MANDATORY
	TARGETS
		nsdk_syncCheck SHARED libnsdk_syncCheck.so
	FILES
		${syncCheck_PUBLIC_HEADERS}
)

target_link_libraries(nsdk_syncCheck PUBLIC m sndfile)
target_link_libraries(nsdk_syncCheck_test nsdk_syncCheck)

target_include_directories(nsdk_syncCheck PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS nsdk_syncCheck DESTINATION ${CMAKE_INSTALL_LIBDIR})
