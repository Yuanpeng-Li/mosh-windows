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
    locale_utils_win32.cc locale_utils.h)
  # Still missing on Windows, and the reason mosh-client and mosh-server are
  # not built there yet: select.h is pselect plus POSIX signals, and
  # pty_compat is forkpty plus ioctl. Those are the event loop and the ConPTY
  # host, and each is its own piece of work.
else()
  list(APPEND _moshutil_sources
    swrite.cc       swrite.h
    locale_utils.cc locale_utils.h
    select.cc       select.h
    pty_compat.cc   pty_compat.h)
endif()

add_library(moshutil STATIC ${_moshutil_sources})
target_link_libraries(moshutil PUBLIC mosh_common moshutil_portable)
add_dependencies(moshutil mosh_version)

if(MOSH_FORKPTY_LIB)
  target_link_libraries(moshutil PUBLIC ${MOSH_FORKPTY_LIB})
endif()
