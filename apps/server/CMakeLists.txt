set(WEB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/web)
file(GLOB_RECURSE WEB_FILES RELATIVE ${WEB_ROOT} CONFIGURE_DEPENDS ${WEB_ROOT}/*)
list(TRANSFORM WEB_FILES PREPEND ${WEB_ROOT}/ OUTPUT_VARIABLE WEB_FILE_PATHS)
string(REPLACE ";" "|" WEB_FILE_LIST "${WEB_FILES}")

set(WEB_ASSETS_CPP ${CMAKE_CURRENT_BINARY_DIR}/WebAssets.cpp)
add_custom_command(
	OUTPUT ${WEB_ASSETS_CPP}
	COMMAND ${CMAKE_COMMAND} -DROOT=${WEB_ROOT} -DFILES=${WEB_FILE_LIST} -DOUTPUT=${WEB_ASSETS_CPP}
		-P ${PROJECT_SOURCE_DIR}/cmake/EmbedAssets.cmake
	DEPENDS ${WEB_FILE_PATHS} ${PROJECT_SOURCE_DIR}/cmake/EmbedAssets.cmake
	COMMENT "Embedding web UI"
	VERBATIM
)

add_executable(sync_analyzer_server
	main.cpp
	AnalyzerService.cpp
	AnalyzerService.h
	WebAssets.h
	${WEB_ASSETS_CPP}
)
target_include_directories(sync_analyzer_server PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${PROJECT_SOURCE_DIR}/third_party/CLI11
	${PROJECT_SOURCE_DIR}/third_party/httplib
	${PROJECT_SOURCE_DIR}/third_party/json
)
target_compile_definitions(sync_analyzer_server PRIVATE CPPHTTPLIB_THREAD_POOL_COUNT=16)
target_link_libraries(sync_analyzer_server PRIVATE synccheck)
if(WIN32)
	target_link_libraries(sync_analyzer_server PRIVATE ws2_32)
endif()

install(TARGETS sync_analyzer_server DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT applications)
if(UNIX AND NOT APPLE)
	# Menu entry (launcher mode), icon and the optional systemd user service.
	install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/sync-analyzer.desktop
		DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT applications)
	install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/sync-analyzer.svg
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT applications)
	install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/sync-analyzer.service
		DESTINATION lib/systemd/user COMPONENT applications)
endif()
install(FILES ${PROJECT_SOURCE_DIR}/third_party/httplib/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME cpp-httplib.txt COMPONENT applications)
install(FILES ${PROJECT_SOURCE_DIR}/third_party/json/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME nlohmann-json.txt COMPONENT applications)
install(FILES ${WEB_ROOT}/vendor/uPlot.LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME uPlot.txt COMPONENT applications)
