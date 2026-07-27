# Protobuf code generation.
#
# Output goes to ${CMAKE_BINARY_DIR}/src/protobufs/ so that the existing
# `#include "src/protobufs/foo.pb.h"` spellings resolve against the binary dir
# that mosh_common puts on the include path.

set(MOSH_PROTOS
  userinput.proto
  hostinput.proto
  transportinstruction.proto)

set(_gen_dir "${CMAKE_BINARY_DIR}/src/protobufs")
file(MAKE_DIRECTORY "${_gen_dir}")

set(_gen_srcs "")
foreach(_proto IN LISTS MOSH_PROTOS)
  get_filename_component(_base "${_proto}" NAME_WE)
  set(_cc "${_gen_dir}/${_base}.pb.cc")
  set(_h  "${_gen_dir}/${_base}.pb.h")
  add_custom_command(
    OUTPUT "${_cc}" "${_h}"
    COMMAND ${MOSH_PROTOC}
            --cpp_out=${_gen_dir}
            -I${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/${_proto}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_proto}"
    COMMENT "protoc ${_proto}"
    VERBATIM)
  list(APPEND _gen_srcs "${_cc}" "${_h}")
endforeach()

add_library(moshprotos STATIC ${_gen_srcs})
# All three .proto files declare `optimize_for = LITE_RUNTIME`, so the lite
# runtime is sufficient -- and it avoids dragging in descriptors, reflection
# and (on protobuf 4+) a large slice of Abseil.
if(TARGET protobuf::libprotobuf-lite)
  target_link_libraries(moshprotos PUBLIC mosh_common protobuf::libprotobuf-lite)
else()
  target_link_libraries(moshprotos PUBLIC mosh_common protobuf::libprotobuf)
endif()
# The generated .pb.cc files include their own header unqualified.
target_include_directories(moshprotos PUBLIC "${_gen_dir}")

# Generated code is not ours to lint. On MSVC a bare /w after mosh_common's
# /W3 only earns a D9025 "overriding" warning, so silence the specific
# diagnostics protoc's output provokes instead.
if(MSVC)
  target_compile_options(moshprotos PRIVATE /wd4244 /wd4267 /wd4996 /wd4127)
else()
  target_compile_options(moshprotos PRIVATE -w)
endif()
