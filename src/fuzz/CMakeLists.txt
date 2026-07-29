# src/fuzz/Makefile.am's `if ENABLE_FUZZING` block.
#
# These have no main(); they are libFuzzer entry points, so the sanitizer flag
# has to reach both the compile and the link. The caller has already checked
# that the compiler accepts it.

add_executable(terminal_parser_fuzzer terminal_parser_fuzzer.cc)
target_link_libraries(terminal_parser_fuzzer PRIVATE
  mosh_common moshterminal moshutil)

add_executable(terminal_fuzzer terminal_fuzzer.cc)
target_link_libraries(terminal_fuzzer PRIVATE
  mosh_common moshterminal moshutil moshstatesync moshprotos)

foreach(_t terminal_parser_fuzzer terminal_fuzzer)
  target_compile_options(${_t} PRIVATE ${MOSH_FUZZING_FLAGS})
  target_link_options(${_t} PRIVATE ${MOSH_FUZZING_FLAGS})
  add_dependencies(${_t} mosh_version)
endforeach()
