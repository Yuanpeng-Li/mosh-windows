# The `mosh` launcher. This is the command users actually run -- mosh-client
# alone is not a usable install -- so the CMake build has to produce it too.
#
# There are two of them. The original is Perl driving ssh; the Windows one is
# PowerShell, because Perl is not present on a stock Windows and because the
# quoting rules differ enough that one script cannot serve both. mosh.exe is a
# small native launcher for the PowerShell one: `mosh` has to be an .exe both
# for winget's portable installer and because PowerShell splits an argument
# like --client=C:\path at the colon when a .ps1 is invoked with -File.

if(WIN32)
  add_executable(mosh mosh-launcher.cc)
  target_link_libraries(mosh PRIVATE mosh_common)
  install(TARGETS mosh RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  # mosh.exe looks for mosh.ps1 beside itself, so the two travel together.
  install(FILES mosh.ps1 mosh.cmd DESTINATION ${CMAKE_INSTALL_BINDIR})

  # Keep them beside the binaries in the build tree as well, so a build tree
  # can be run from without installing.
  add_custom_command(TARGET mosh POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/mosh.ps1" "$<TARGET_FILE_DIR:mosh>/mosh.ps1"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/mosh.cmd" "$<TARGET_FILE_DIR:mosh>/mosh.cmd"
    VERBATIM)
  return()
endif()

set(_mosh_script "${CMAKE_CURRENT_BINARY_DIR}/mosh")

add_custom_command(
  OUTPUT "${_mosh_script}"
  COMMAND ${CMAKE_COMMAND}
          -DIN=${CMAKE_CURRENT_SOURCE_DIR}/mosh.pl
          -DOUT=${_mosh_script}
          -DSRC=${CMAKE_SOURCE_DIR}
          -DPKG_NAME=${PACKAGE_NAME}
          -DPKG_VER=${PACKAGE_VERSION}
          -P ${CMAKE_SOURCE_DIR}/cmake/GenMoshScript.cmake
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mosh.pl"
  COMMENT "Generating mosh launcher"
  VERBATIM)

add_custom_target(mosh_script ALL DEPENDS "${_mosh_script}")

find_package(Perl QUIET)
if(PERL_FOUND)
  # scripts/Makefile.am runs `perl -Mdiagnostics -c mosh.pl` as part of the
  # build; keep that check rather than shipping a script that does not parse.
  add_custom_command(TARGET mosh_script POST_BUILD
    COMMAND ${PERL_EXECUTABLE} -Mdiagnostics -c "${CMAKE_CURRENT_SOURCE_DIR}/mosh.pl"
    COMMENT "Syntax-checking mosh.pl"
    VERBATIM)
endif()

install(PROGRAMS "${_mosh_script}" DESTINATION ${CMAKE_INSTALL_BINDIR})
