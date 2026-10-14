configure_file(resources/branding/MacDependencies.md.in "${CMAKE_BINARY_DIR}/generated/MACOS-DEPENDENCIES.md" @ONLY)
# macOS resources; framework deployment is performed by package-macos.sh.
add_custom_command(TARGET SingLilt POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/language" "$<TARGET_BUNDLE_CONTENT_DIR:SingLilt>/Resources/language"
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_BUNDLE_CONTENT_DIR:SingLilt>/Resources/assets"
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/licenses" "$<TARGET_BUNDLE_CONTENT_DIR:SingLilt>/Resources/licenses"
 COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/docs/MACOS.md" "${CMAKE_BINARY_DIR}/generated/MACOS-DEPENDENCIES.md" "$<TARGET_BUNDLE_CONTENT_DIR:SingLilt>/Resources"
 COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "$<TARGET_BUNDLE_CONTENT_DIR:SingLilt>/Resources"
 VERBATIM)
foreach(bank IN ITEMS SALAMANDER_SF2 GENERALUSER_SF2)
 if(EXISTS "${${bank}}")
  if(bank STREQUAL "SALAMANDER_SF2")
   set(bank_name Salamander.sf2)
  else()
   set(bank_name GeneralUser-GS.sf2)
  endif()
  add_custom_command(TARGET SingLilt POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:SingLilt>/Resources/assets/soundfonts"
   COMMAND ${CMAKE_COMMAND} -E copy_if_different "${${bank}}" "$<TARGET_BUNDLE_CONTENT_DIR:SingLilt>/Resources/assets/soundfonts/${bank_name}" VERBATIM)
 endif()
endforeach()

if(EXISTS "${CMAKE_BINARY_DIR}/_deps/CrispEmbed/runtime/crispembed")
 add_custom_command(TARGET SingLilt POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/_deps/CrispEmbed/runtime"
  "$<TARGET_BUNDLE_CONTENT_DIR:SingLilt>/Resources/tools/omr-native" VERBATIM)
endif()
