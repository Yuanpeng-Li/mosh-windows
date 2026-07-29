# Split in two because the halves have different portability.
#
# moshutil_portable is pure computation and builds anywhere, which is what lets
# the terminal library -- and its tests -- be built and run under MSVC before
# the platform layer exists. src/terminal depends on exactly these two headers
# and nothing else from util.
add_library(moshutil_portable STATIC
  compat.h
  utf8.cc         utf8.h
  uniwidth.cc     uniwidth.h      uniwidth_table.h)
target_link_libraries(moshutil_portable PUBLIC mosh_common)
add_dependencies(moshutil_portable mosh_version)

# The rest is per-platform. Whole-file swaps rather than #ifdefs wherever the
# two implementations answer different questions rather than spelling the same
# answer differently -- locale_utils being the clearest case.
set(_moshutil_sources
  timestamp.cc    timestamp.h
  dos_assert.h
  fatal_assert.h)

if(WIN32)
  list(APPEND _moshutil_sources
    swrite_win32.cc       swrite.h
    locale_utils_win32.cc locale_utils.h
    select_win32.cc       select.h
    console_win32.cc      console.h
    ptyhost_win32.cc      ptyhost.h
    compat_win32.cc)
  # No pty_compat: forkpty and the winsize ioctls have no Windows spelling at
  # all, and ptyhost_win32 answers the same question with a pseudoconsole.
else()
  list(APPEND _moshutil_sources
    swrite.cc       swrite.h
    locale_utils.cc locale_utils.h
    select.cc       select.h
    console_posix.cc console.h
    ptyhost_posix.cc ptyhost.h
    pty_compat.cc   pty_compat.h)
endif()

add_library(moshutil STATIC ${_moshutil_sources})
target_link_libraries(moshutil PUBLIC mosh_common moshutil_portable)
add_dependencies(moshutil mosh_version)

if(MOSH_FORKPTY_LIB)
  target_link_libraries(moshutil PUBLIC ${MOSH_FORKPTY_LIB})
endif()
