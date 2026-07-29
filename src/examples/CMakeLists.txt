# src/examples/Makefile.am's `if BUILD_EXAMPLES` block.
#
# These are noinst_PROGRAMS: never installed, built only to be run by hand.
# They are POSIX-only -- ntester.cc drives a terminal through <termios.h> and
# pty_compat -- so the whole directory is skipped on Windows by the caller.

add_executable(example-encrypt encrypt.cc)
set_target_properties(example-encrypt PROPERTIES OUTPUT_NAME encrypt)
target_link_libraries(example-encrypt PRIVATE mosh_common moshcrypto moshutil)

add_executable(example-decrypt decrypt.cc)
set_target_properties(example-decrypt PROPERTIES OUTPUT_NAME decrypt)
target_link_libraries(example-decrypt PRIVATE mosh_common moshcrypto moshutil)

add_executable(example-parse parse.cc)
set_target_properties(example-parse PROPERTIES OUTPUT_NAME parse)
target_link_libraries(example-parse PRIVATE mosh_common moshterminal moshutil)

add_executable(example-termemu termemu.cc)
set_target_properties(example-termemu PROPERTIES OUTPUT_NAME termemu)
target_link_libraries(example-termemu PRIVATE
  mosh_common moshterminal moshutil moshstatesync moshprotos)

add_executable(example-ntester ntester.cc)
set_target_properties(example-ntester PROPERTIES OUTPUT_NAME ntester)
target_link_libraries(example-ntester PRIVATE
  mosh_common moshstatesync moshterminal moshnetwork moshcrypto moshprotos moshutil m)

# benchmark_LDADD names ../frontend/terminaloverlay.o -- a single object out of
# a program that has no library. The CMake build has no such object either, so
# compile the translation unit into this target instead of reaching for one.
add_executable(example-benchmark benchmark.cc ../frontend/terminaloverlay.cc)
set_target_properties(example-benchmark PROPERTIES OUTPUT_NAME benchmark)
target_link_libraries(example-benchmark PRIVATE
  mosh_common moshstatesync moshterminal moshprotos moshnetwork moshcrypto moshutil m)

foreach(_t example-encrypt example-decrypt example-parse example-termemu
          example-ntester example-benchmark)
  target_compile_definitions(${_t} PRIVATE ${MOSH_POSIX_FEATURE_DEFS})
  add_dependencies(${_t} mosh_version)
  if(MOSH_HARDEN AND MOSH_HARDEN_LINK_FLAGS)
    target_link_options(${_t} PRIVATE ${MOSH_HARDEN_LINK_FLAGS})
  endif()
  if(MOSH_EXTRA_LIBS)
    target_link_libraries(${_t} PRIVATE ${MOSH_EXTRA_LIBS})
  endif()
endforeach()
