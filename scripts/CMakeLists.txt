# The `mosh` launcher. This is the command users actually run -- mosh-client
# alone is not a usable install -- so the CMake build has to produce it too.
#
# It is Perl and drives ssh, so it is POSIX-only; the Windows port gets its own
# launcher (see the port plan). Mirrors scripts/Makefile.am, including the
# `perl -c` syntax check that build performs.

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
