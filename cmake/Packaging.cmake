# Third party licenses shipped with the applications.
install(FILES third_party/miniaudio/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME miniaudio.txt COMPONENT applications)
install(FILES third_party/CLI11/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME CLI11.txt COMPONENT applications)
install(FILES kissfft/COPYING DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses RENAME kissfft.txt COMPONENT applications)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR} COMPONENT applications)

set(CPACK_PACKAGE_NAME "synccheck")
set(CPACK_PACKAGE_VENDOR "Embedded Softworks, s.r.o.")
set(CPACK_PACKAGE_CONTACT "Stanislav Ruzani <stanislav.ruzani@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Audio synchronization measurement with a web UI")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Sync Analyzer")
set(CPACK_RESOURCE_FILE_README "${PROJECT_SOURCE_DIR}/README.md")
set(CPACK_STRIP_FILES ON)

# Installers only contain the applications; the development files are
# installed with: cmake --install <build> --component development
set(CPACK_COMPONENTS_ALL applications)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENT_APPLICATIONS_DISPLAY_NAME "Sync Analyzer")
set(CPACK_COMPONENT_APPLICATIONS_REQUIRED ON)

if(WIN32)
	set(CPACK_GENERATOR "ZIP")
	find_program(MAKENSIS_EXECUTABLE makensis)
	if(MAKENSIS_EXECUTABLE)
		list(APPEND CPACK_GENERATOR "NSIS")
	endif()
	set(CPACK_NSIS_DISPLAY_NAME "Sync Analyzer")
	set(CPACK_NSIS_PACKAGE_NAME "Sync Analyzer")
	set(CPACK_NSIS_MODIFY_PATH ON)
	set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
	set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\sync_analyzer_tray.exe")
	set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/packaging/windows/sync-analyzer.ico")
	set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/packaging/windows/sync-analyzer.ico")
	# Start menu entry: the tray app (the server behind a notification area icon), which opens the
	# browser; its menu can start it with Windows. The installer can start it at the end.
	set(CPACK_NSIS_CREATE_ICONS_EXTRA
		"CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Sync Analyzer.lnk' '$INSTDIR\\\\bin\\\\sync_analyzer_tray.exe'")
	set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$SMPROGRAMS\\\\$MUI_TEMP\\\\Sync Analyzer.lnk'")
	set(CPACK_NSIS_MUI_FINISHPAGE_RUN "sync_analyzer_tray.exe")
	# A running tray app would keep its files from being replaced or removed. Starting with Windows
	# is turned off on uninstall, but kept when an update runs the old uninstaller (silently).
	set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS "nsExec::Exec 'taskkill /IM sync_analyzer_tray.exe /F'")
	set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "nsExec::Exec 'taskkill /IM sync_analyzer_tray.exe /F'
  IfSilent +2
  DeleteRegValue HKCU 'Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run' 'Sync Analyzer'")
elseif(APPLE)
	set(CPACK_GENERATOR "TGZ")
else()
	set(CPACK_GENERATOR "TGZ")
	find_program(DPKG_EXECUTABLE dpkg-deb)
	if(DPKG_EXECUTABLE)
		list(APPEND CPACK_GENERATOR "DEB")
	endif()
	find_program(RPMBUILD_EXECUTABLE rpmbuild)
	if(RPMBUILD_EXECUTABLE)
		list(APPEND CPACK_GENERATOR "RPM")
	endif()
	set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
	set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
	set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
	# Audio is loaded at run time (ALSA, PulseAudio/PipeWire).
	set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libasound2t64 | libasound2")
	set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
	set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
endif()

include(CPack)
