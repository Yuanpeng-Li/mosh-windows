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

# The rest is POSIX and has no Windows implementation yet: select.h is built on
# pselect, pty_compat on forkpty and ioctl, swrite on write(2), timestamp on
# clock_gettime, locale_utils on nl_langinfo and unsetenv.
if(NOT WIN32)
  add_library(moshutil STATIC
    locale_utils.cc locale_utils.h
    swrite.cc       swrite.h
    select.cc       select.h
    timestamp.cc    timestamp.h
    pty_compat.cc   pty_compat.h
    dos_assert.h
    fatal_assert.h)
  target_link_libraries(moshutil PUBLIC mosh_common moshutil_portable)
  add_dependencies(moshutil mosh_version)

  if(MOSH_FORKPTY_LIB)
    target_link_libraries(moshutil PUBLIC ${MOSH_FORKPTY_LIB})
  endif()
endif()
