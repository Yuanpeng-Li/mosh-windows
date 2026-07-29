# Generate the `mosh` launcher from scripts/mosh.pl, mirroring the rule in
# scripts/Makefile.am: @VERSION@ becomes the `git describe` build version and
# @PACKAGE_STRING@ becomes the package string.
#
# PKG_NAME and PKG_VER arrive separately rather than as a pre-joined
# "mosh 1.4.0": a -D value containing a space does not survive the trip through
# add_custom_command intact, which silently leaves @PACKAGE_STRING@ unexpanded
# in the generated script.
#
# Invoked as: cmake -DIN=... -DOUT=... -DSRC=... -DPKG_NAME=... -DPKG_VER=... -P GenMoshScript.cmake

set(_package_string "${PKG_NAME} ${PKG_VER}")
set(_version "${_package_string}")

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${SRC}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --dirty --always
    WORKING_DIRECTORY "${SRC}"
    OUTPUT_VARIABLE _described
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _rc)
  if(_rc EQUAL 0 AND _described)
    set(_version "${_described}")
  endif()
endif()

file(READ "${IN}" _content)
string(REPLACE "@VERSION@" "${_version}" _content "${_content}")
string(REPLACE "@PACKAGE_STRING@" "${_package_string}" _content "${_content}")

set(_existing "")
if(EXISTS "${OUT}")
  file(READ "${OUT}" _existing)
endif()
if(NOT _existing STREQUAL _content)
  file(WRITE "${OUT}" "${_content}")
endif()

# install(PROGRAMS) would set the mode on the installed copy only, and the
# scripted tests run the one in the build tree. Unconditional because a build
# that skipped the write above still has to end up with it set.
if(NOT WIN32)
  execute_process(COMMAND chmod a+x "${OUT}")
endif()
