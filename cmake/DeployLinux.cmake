# Linux runtime data and distribution-managed shared libraries.
# Copyright (c) 2026 Herbert Yeung
# SPDX-License-Identifier: MIT

add_custom_command(TARGET SingLilt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/language" "$<TARGET_FILE_DIR:SingLilt>/language"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:SingLilt>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/licenses" "$<TARGET_FILE_DIR:SingLilt>/licenses"
    VERBATIM)

include(GNUInstallDirs)
set_target_properties(SingLilt PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS SingLilt RUNTIME DESTINATION "${CMAKE_INSTALL_LIBDIR}/singlilt")
install(DIRECTORY language assets licenses DESTINATION "${CMAKE_INSTALL_LIBDIR}/singlilt")
install(FILES README.md THIRD_PARTY_NOTICES.md LICENSE DESTINATION "${CMAKE_INSTALL_LIBDIR}/singlilt")
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
