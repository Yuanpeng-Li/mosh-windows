add_library(moshutil STATIC
  locale_utils.cc locale_utils.h
  swrite.cc       swrite.h
  select.cc       select.h
  timestamp.cc    timestamp.h
  pty_compat.cc   pty_compat.h
  utf8.cc         utf8.h
  dos_assert.h
  fatal_assert.h)

target_link_libraries(moshutil PUBLIC mosh_common)
add_dependencies(moshutil mosh_version)

if(MOSH_FORKPTY_LIB)
  target_link_libraries(moshutil PUBLIC ${MOSH_FORKPTY_LIB})
endif()
