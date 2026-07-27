# CMake build for mosh.
#
# Why this exists: the autotools build cannot produce a native MSVC binary, and
# the Windows port needs one. This build is intended to be a peer of the
# autotools build, not a replacement -- ./configure && make must keep working
# on Linux and macOS exactly as before.
#
#   Linux/macOS:  cmake -B build -G Ninja && cmake --build build
#   Windows:      cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
#
# Layout note: generated headers (config.h, version.h, *.pb.h) land under
# ${CMAKE_BINARY_DIR}/src/..., matching the "src/include/config.h" and
# "src/protobufs/foo.pb.h" spellings the sources already use. The binary dir is
# placed BEFORE the source dir on the include path so that an in-tree autotools
# build lying around cannot shadow the CMake-generated headers.

cmake_minimum_required(VERSION 3.16)

# Single source of truth for the version: parse it out of configure.ac so the
# two build systems cannot drift apart.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/configure.ac" _mosh_configure_ac)
string(REGEX MATCH "AC_INIT\\(\\[mosh\\][ \t]*,[ \t]*\\[([0-9]+\\.[0-9]+\\.[0-9]+)\\][ \t]*,[ \t]*\\[([^]]*)\\]"
       _mosh_ac_init_match "${_mosh_configure_ac}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "CMakeLists.txt: could not parse AC_INIT version out of configure.ac")
endif()
set(MOSH_VERSION "${CMAKE_MATCH_1}")
set(MOSH_BUGREPORT "${CMAKE_MATCH_2}")
unset(_mosh_configure_ac)

# mosh itself is C++ only, but CMake's Check* modules compile C probe files,
# so C has to be enabled for check_include_file/check_type_size to work.
project(mosh VERSION ${MOSH_VERSION} LANGUAGES C CXX)

# An in-source configure would write ${CMAKE_BINARY_DIR}/src/include/config.h
# straight over the autotools one, and drop CMake-generated Makefiles on top of
# the automake ones. That silently destroys a working autotools tree, so refuse.
if(CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
  message(FATAL_ERROR
    "In-source builds would overwrite the autotools Makefiles and "
    "src/include/config.h. Use an out-of-tree build, e.g.:\n"
    "  cmake -S . -B build -G Ninja")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# configure.ac uses AX_CXX_COMPILE_STDCXX([17]) with the default "ext" mode,
# i.e. -std=gnu++17. Match that so GNU extensions stay available on POSIX.
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# The autotools build compiles with plain `-g -O2` and never defines NDEBUG, so
# its assert()s stay live -- including the bounds checks guarding the memcpy
# into the fixed-size AEAD buffers in src/crypto/crypto.cc. CMake's optimised
# configurations add -DNDEBUG, which would make the two build systems ship
# materially different binaries. Strip it back out.
option(MOSH_KEEP_ASSERTS "Keep assert() live in optimised builds, as the autotools build does" ON)
if(MOSH_KEEP_ASSERTS)
  foreach(_cfg RELEASE RELWITHDEBINFO MINSIZEREL)
    foreach(_lang C CXX)
      string(REPLACE "-DNDEBUG" "" CMAKE_${_lang}_FLAGS_${_cfg} "${CMAKE_${_lang}_FLAGS_${_cfg}}")
      string(REPLACE "/DNDEBUG" "" CMAKE_${_lang}_FLAGS_${_cfg} "${CMAKE_${_lang}_FLAGS_${_cfg}}")
    endforeach()
  endforeach()
endif()

option(MOSH_BUILD_CLIENT   "Build mosh-client" ON)
option(MOSH_BUILD_SERVER   "Build mosh-server" ON)
# The example programs under src/examples are POSIX-only (ntester.cc reaches
# straight into Network::fds()). Not wired up yet -- the option exists so the
# autotools --enable-examples equivalent has a home once they are ported.
option(MOSH_BUILD_EXAMPLES "Build the example programs (POSIX only, not yet implemented)" OFF)
if(MOSH_BUILD_EXAMPLES)
  message(FATAL_ERROR "MOSH_BUILD_EXAMPLES is not implemented in the CMake build yet; use the autotools build with --enable-examples")
endif()
option(MOSH_HARDEN         "Enable hardening flags (matches configure --enable-hardening)" ON)

include(CTest)
include(GNUInstallDirs)

# ------------------------------------------------------------ dependencies --

include(CheckIncludeFile)

find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
  # Debian's protobuf 3.21 ships no CMake config package; fall back to the
  # module shipped with CMake.
  find_package(Protobuf REQUIRED)
endif()
if(TARGET protobuf::protoc)
  set(MOSH_PROTOC $<TARGET_FILE:protobuf::protoc>)
elseif(Protobuf_PROTOC_EXECUTABLE)
  set(MOSH_PROTOC "${Protobuf_PROTOC_EXECUTABLE}")
else()
  message(FATAL_ERROR "protoc not found")
endif()

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

# Terminal capability database. On Windows there is none -- the client uses a
# built-in xterm-256color table instead (see src/terminal).
set(MOSH_TINFO_LIBS "")
if(NOT WIN32)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(TINFO QUIET tinfo)
    if(NOT TINFO_FOUND)
      pkg_check_modules(TINFO QUIET ncursesw)
    endif()
    if(NOT TINFO_FOUND)
      pkg_check_modules(TINFO QUIET ncurses)
    endif()
  endif()
  set(MOSH_TINFO_INCLUDE_DIRS "")
  set(MOSH_TINFO_LIBRARY_DIRS "")
  if(TINFO_FOUND)
    # configure.ac's PKG_CHECK_MODULES([TINFO], [tinfo]/[ncurses]) branches
    # define HAVE_CURSES_H and nothing else, so the sources take the plain
    # <curses.h> path. Match that exactly rather than probing harder here --
    # picking a different header than the autotools build is a divergence, not
    # an improvement.
    set(MOSH_TINFO_LIBS ${TINFO_LIBRARIES})
    set(MOSH_TINFO_INCLUDE_DIRS ${TINFO_INCLUDE_DIRS})
    set(MOSH_TINFO_LIBRARY_DIRS ${TINFO_LIBRARY_DIRS})
    set(HAVE_CURSES_H 1)
  else()
    # No pkg-config answer: this is configure.ac's AX_WITH_CURSES fallback, and
    # it is the path that has to work out which spelling of the header exists.
    find_package(Curses REQUIRED)
    set(MOSH_TINFO_LIBS ${CURSES_LIBRARIES})
    # Without forwarding these, terminaldisplayinit.cc cannot find the header
    # on any layout that keeps it off the default include path -- Homebrew
    # ncurses on macOS being the common case.
    set(MOSH_TINFO_INCLUDE_DIRS ${CURSES_INCLUDE_DIRS})

    # terminaldisplayinit.cc:42-58 and mosh-client.cc:55-70 select their header
    # from these five macros, in this priority order.
    set(CMAKE_REQUIRED_INCLUDES ${MOSH_TINFO_INCLUDE_DIRS})
    check_include_file(ncursesw/curses.h HAVE_NCURSESW_CURSES_H)
    check_include_file(ncursesw.h        HAVE_NCURSESW_H)
    check_include_file(ncurses/curses.h  HAVE_NCURSES_CURSES_H)
    check_include_file(ncurses.h         HAVE_NCURSES_H)
    check_include_file(curses.h          HAVE_CURSES_H)
    unset(CMAKE_REQUIRED_INCLUDES)
    if(NOT HAVE_NCURSESW_CURSES_H AND NOT HAVE_NCURSESW_H AND NOT HAVE_NCURSES_CURSES_H
       AND NOT HAVE_NCURSES_H AND NOT HAVE_CURSES_H)
      message(FATAL_ERROR "requires either tinfo, NcursesW or Ncurses")
    endif()
  endif()
endif()

# ------------------------------------------------------------- config.h -----

include(MoshChecks)

# mosh's own OCB implementation on top of OpenSSL's AES is the configure
# default (--with-crypto-library=openssl); ocb_openssl.cc is only used with
# openssl-with-openssl-ocb.
set(USE_OPENSSL_AES 1)
set(MOSH_USE_OPENSSL_OCB OFF CACHE BOOL "Use OpenSSL's EVP_aes_128_ocb instead of the bundled OCB")

set(PACKAGE         "mosh")
set(PACKAGE_NAME    "mosh")
set(PACKAGE_TARNAME "mosh")
set(PACKAGE_VERSION "${MOSH_VERSION}")
set(PACKAGE_STRING  "mosh ${MOSH_VERSION}")
set(PACKAGE_BUGREPORT "${MOSH_BUGREPORT}")
set(PACKAGE_URL     "")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in"
               "${CMAKE_BINARY_DIR}/src/include/config.h" @ONLY)

# ------------------------------------------------------------- version.h ----
# Mirrors Makefile.am's VERSION.stamp rule: prefer `git describe`, fall back to
# the package string for tarball builds. Regenerated at build time so the
# recorded commit does not go stale between configures.
add_custom_target(mosh_version ALL
  BYPRODUCTS "${CMAKE_BINARY_DIR}/src/include/version.h"
  COMMAND ${CMAKE_COMMAND}
          -DOUT=${CMAKE_BINARY_DIR}/src/include/version.h
          -DSRC=${CMAKE_SOURCE_DIR}
          -DFALLBACK=${PACKAGE_STRING}
          -P ${CMAKE_SOURCE_DIR}/cmake/GenVersionHeader.cmake
  COMMENT "Generating version.h"
  VERBATIM)

# ------------------------------------------------------- common settings ----

add_library(mosh_common INTERFACE)
target_include_directories(mosh_common BEFORE INTERFACE
  "${CMAKE_BINARY_DIR}"                # "src/include/config.h", "src/protobufs/*.pb.h"
  "${CMAKE_BINARY_DIR}/src/include"    # bare #include "config.h" (e.g. src/crypto/prng.h)
  "${CMAKE_SOURCE_DIR}")               # "src/..." spellings in the sources
target_compile_definitions(mosh_common INTERFACE HAVE_CONFIG_H)

# Platform defines belong to the platform, not to one compiler: a MinGW or
# clang-with-GNU-driver build needs NOMINMAX and the winsock2 guard every bit as
# much as MSVC does.
if(WIN32)
  target_compile_definitions(mosh_common INTERFACE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000006)   # NTDDI_WIN10_RS5 -- gates the ConPTY API
  target_link_libraries(mosh_common INTERFACE ws2_32)
endif()

if(MSVC)
  target_compile_definitions(mosh_common INTERFACE
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS)
  # /Zc:__cplusplus: without it MSVC reports __cplusplus as 199711L regardless
  # of /std, which makes every C++ version probe (and any such check in the
  # sources) give the wrong answer.
  target_compile_options(mosh_common INTERFACE /W3 /utf-8 /permissive- /Zc:__cplusplus)
else()
  include(CheckCXXCompilerFlag)
  include(CheckLinkerFlag OPTIONAL RESULT_VARIABLE _have_check_linker_flag)

  # configure.ac does not hardcode any of these -- it probes each one through
  # scripts/wrap-compiler-for-flag-check, which compiles with -Werror and drops
  # whatever the toolchain rejects. Hardcoding them breaks real platforms:
  # Apple's linker rejects the ELF -Wl,-z,* forms outright, and clang does not
  # universally accept --param ssp-buffer-size=1.
  function(_mosh_try_cxx_flag flag var)
    string(MAKE_C_IDENTIFIER "MOSH_CXXFLAG_${flag}" _cache)
    set(CMAKE_REQUIRED_FLAGS "-Werror")
    check_cxx_compiler_flag("${flag}" ${_cache})
    unset(CMAKE_REQUIRED_FLAGS)
    if(${_cache})
      set(${var} "${${var}};${flag}" PARENT_SCOPE)
    endif()
  endfunction()

  set(_mosh_cxx_flags "")
  _mosh_try_cxx_flag(-Wall _mosh_cxx_flags)
  # configure.ac's MISC_CXXFLAGS. -fno-default-inline is unknown to modern clang.
  _mosh_try_cxx_flag(-fno-default-inline _mosh_cxx_flags)
  _mosh_try_cxx_flag(-pipe _mosh_cxx_flags)

  if(MOSH_HARDEN)
    _mosh_try_cxx_flag(-fno-strict-overflow _mosh_cxx_flags)
    _mosh_try_cxx_flag(-fstack-protector-all _mosh_cxx_flags)
    _mosh_try_cxx_flag(-Wstack-protector _mosh_cxx_flags)
    _mosh_try_cxx_flag("--param;ssp-buffer-size=1" _mosh_cxx_flags)
  endif()
  target_compile_options(mosh_common INTERFACE ${_mosh_cxx_flags})

  if(MOSH_HARDEN)
    # _FORTIFY_SOURCE needs optimisation, and redefining it warns on the
    # distros (Ubuntu 24.04, GCC 12+ specs) that already predefine it -- often
    # at level 3, which a bare -D would silently downgrade. Undefine first.
    target_compile_options(mosh_common INTERFACE
      $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=2>)

    # -fPIE must reach the static libraries too, not just the executables that
    # link them; autotools puts it in every directory's AM_CXXFLAGS. Without
    # this it links only because most distro toolchains default to PIE.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON PARENT_SCOPE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    set(MOSH_HARDEN_LINK_FLAGS "")
    foreach(_lf "-pie" "LINKER:-z,relro" "LINKER:-z,now")
      if(_have_check_linker_flag OR COMMAND check_linker_flag)
        string(MAKE_C_IDENTIFIER "MOSH_LDFLAG_${_lf}" _lcache)
        check_linker_flag(CXX "${_lf}" ${_lcache})
        if(${_lcache})
          list(APPEND MOSH_HARDEN_LINK_FLAGS "${_lf}")
        endif()
      endif()
    endforeach()
    set(MOSH_HARDEN_LINK_FLAGS "${MOSH_HARDEN_LINK_FLAGS}" CACHE INTERNAL "")
  endif()
endif()

add_subdirectory(src/protobufs)
add_subdirectory(src/util)
add_subdirectory(src/crypto)
add_subdirectory(src/terminal)
add_subdirectory(src/network)
add_subdirectory(src/statesync)
add_subdirectory(src/frontend)

if(BUILD_TESTING)
  add_subdirectory(src/tests)
endif()

if(NOT WIN32 AND MOSH_BUILD_CLIENT)
  add_subdirectory(scripts)
endif()

# man/Makefile.am installs these unconditionally.
if(NOT WIN32)
  install(FILES man/mosh.1 man/mosh-client.1 man/mosh-server.1
          DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endif()

message(STATUS "")
message(STATUS "mosh ${MOSH_VERSION} (CMake build)")
message(STATUS "  platform     : ${CMAKE_SYSTEM_NAME} / ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  client       : ${MOSH_BUILD_CLIENT}")
message(STATUS "  server       : ${MOSH_BUILD_SERVER}")
message(STATUS "  protoc       : ${MOSH_PROTOC}")
message(STATUS "  terminfo libs: ${MOSH_TINFO_LIBS}")
message(STATUS "")
