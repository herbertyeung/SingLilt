# Linux runtime data and distribution-managed shared libraries.
# Copyright (c) 2026 Herbert Yeung
# SPDX-License-Identifier: MIT

add_custom_command(TARGET SingLilt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/language" "$<TARGET_FILE_DIR:SingLilt>/language"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:SingLilt>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/licenses" "$<TARGET_FILE_DIR:SingLilt>/licenses"
    VERBATIM)

include(GNUInstallDirs)
# Qt libraries are replaceable sidecars, never dependencies on the source checkout.
set_target_properties(SingLilt PROPERTIES
    INSTALL_RPATH "$ORIGIN/qt/lib"
    INSTALL_RPATH_USE_LINK_PATH FALSE)
install(TARGETS SingLilt RUNTIME DESTINATION "${CMAKE_INSTALL_LIBDIR}/singlilt")
qt_generate_deploy_script(TARGET SingLilt OUTPUT_SCRIPT linux_deploy_script
    CONTENT "
set(app_dir \"${CMAKE_INSTALL_LIBDIR}/singlilt\")
if(IS_ABSOLUTE \"\${app_dir}\")
    file(RELATIVE_PATH app_dir \"\${QT_DEPLOY_PREFIX}\" \"\${app_dir}\")
endif()
set(QT_DEPLOY_BIN_DIR \"\${app_dir}\")
set(QT_DEPLOY_LIB_DIR \"\${app_dir}/qt/lib\")
set(QT_DEPLOY_PLUGINS_DIR \"\${app_dir}/qt/plugins\")
qt_deploy_runtime_dependencies(EXECUTABLE \"$<TARGET_FILE:SingLilt>\" NO_TRANSLATIONS)
")
install(SCRIPT "${linux_deploy_script}")
file(WRITE "${CMAKE_BINARY_DIR}/linux-qt.conf" "[Paths]\nPrefix=qt\nLibraries=lib\nPlugins=plugins\n")
install(FILES "${CMAKE_BINARY_DIR}/linux-qt.conf" DESTINATION "${CMAKE_INSTALL_LIBDIR}/singlilt" RENAME qt.conf)
install(FILES licenses/LGPL-3.0-only.txt licenses/GPL-3.0-only.txt DESTINATION "${CMAKE_INSTALL_LIBDIR}/singlilt/qt/licenses")
install(DIRECTORY language assets licenses DESTINATION "${CMAKE_INSTALL_LIBDIR}/singlilt")
install(FILES README.md THIRD_PARTY_NOTICES.md LICENSE DESTINATION "${CMAKE_INSTALL_LIBDIR}/singlilt")
file(RELATIVE_PATH SINGLILT_BIN_TO_LIB "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_LIBDIR}")
configure_file(packaging/linux/singlilt.in "${CMAKE_BINARY_DIR}/singlilt" @ONLY)
install(PROGRAMS "${CMAKE_BINARY_DIR}/singlilt" DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES packaging/linux/singlilt.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES resources/branding/singlilt-icon.png DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
    RENAME singlilt.png)

set(CPACK_PACKAGE_NAME "singlilt")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Herbert Yeung")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "SingLilt music practice desktop application")
set(CPACK_GENERATOR "TGZ")
include(CPack)
