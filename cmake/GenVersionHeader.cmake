# Generate src/include/version.h, mirroring the VERSION.stamp rule in
# Makefile.am: use `git describe --dirty --always` inside a git checkout,
# otherwise fall back to the package string.
#
# Only rewrites the file when the contents change, so touching it does not
# force a rebuild of everything that includes it.
#
# Invoked as: cmake -DOUT=... -DSRC=... -DFALLBACK=... -P GenVersionHeader.cmake

set(_version "${FALLBACK}")

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

set(_content "#define BUILD_VERSION \"${_version}\"\n")

set(_existing "")
if(EXISTS "${OUT}")
  file(READ "${OUT}" _existing)
endif()

if(NOT _existing STREQUAL _content)
  get_filename_component(_dir "${OUT}" DIRECTORY)
  file(MAKE_DIRECTORY "${_dir}")
  file(WRITE "${OUT}" "${_content}")
endif()
