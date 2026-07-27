# MoshChecks.cmake -- reproduce configure.ac's feature detection.
#
# Every variable set here feeds cmake/config.h.in. The goal is byte-for-byte
# behavioural parity with the autotools config.h on POSIX, and a sane set of
# answers on Win32 where most of these simply do not exist.

include(CheckIncludeFile)
include(CheckIncludeFileCXX)
include(CheckSymbolExists)
include(CheckCXXSymbolExists)
include(CheckCXXSourceCompiles)
include(CheckTypeSize)

# ---------------------------------------------------------------- headers --
# check_include_file uses the C compiler; these are all C headers.
set(_mosh_headers
  fcntl.h inttypes.h langinfo.h limits.h locale.h netdb.h netinet/in.h
  paths.h pty.h stddef.h stdint.h stdio.h stdlib.h strings.h string.h
  sys/ioctl.h sys/random.h sys/resource.h sys/socket.h sys/stat.h
  sys/time.h sys/types.h sys/uio.h termios.h termio.h unistd.h
  utmpx.h util.h libutil.h wchar.h wctype.h endian.h sys/endian.h
  CommonCrypto/CommonCrypto.h)

foreach(_h IN LISTS _mosh_headers)
  # HAVE_SYS_IOCTL_H from sys/ioctl.h, etc.
  string(TOUPPER "${_h}" _var)
  string(REGEX REPLACE "[/.]" "_" _var "${_var}")
  check_include_file("${_h}" HAVE_${_var})
endforeach()

# STDC_HEADERS is vestigial in autoconf but the sources may test it.
if(HAVE_STDLIB_H AND HAVE_STRING_H AND HAVE_STDIO_H)
  set(STDC_HEADERS 1)
endif()

check_include_file_cxx(memory HAVE_MEMORY)
check_include_file_cxx(tr1/memory HAVE_TR1_MEMORY)

# -------------------------------------------------------------- C++ level --
check_cxx_source_compiles("
  #if __cplusplus < 201703L
  #error not C++17
  #endif
  int main(){return 0;}" HAVE_CXX17)

check_cxx_source_compiles("
  #include <memory>
  int main(){ std::shared_ptr<int> p; return 0; }" HAVE_STD_SHARED_PTR)
check_cxx_source_compiles("
  #include <tr1/memory>
  int main(){ std::tr1::shared_ptr<int> p; return 0; }" HAVE_STD_TR1_SHARED_PTR)

# -------------------------------------------------------------- functions --
if(WIN32)
  # None of these exist in the MSVC CRT. Leave every HAVE_* undefined so the
  # sources take their fallback paths, and let the platform layer supply the
  # replacements.
  set(HAVE_CFMAKERAW      OFF)
  set(HAVE_CLOCK_GETTIME  OFF)
  set(HAVE_GETTIMEOFDAY   OFF)
  set(HAVE_GETENTROPY     OFF)
  set(HAVE_GETRANDOM      OFF)
  set(HAVE_POSIX_MEMALIGN OFF)
  set(HAVE_PSELECT        OFF)
  set(HAVE_FORKPTY        OFF)
  set(HAVE_PLEDGE         OFF)
  set(HAVE_MACH_ABSOLUTE_TIME OFF)
else()
  check_symbol_exists(cfmakeraw      "termios.h"              HAVE_CFMAKERAW)
  check_symbol_exists(clock_gettime  "time.h"                 HAVE_CLOCK_GETTIME)
  check_symbol_exists(gettimeofday   "sys/time.h"             HAVE_GETTIMEOFDAY)
  check_symbol_exists(getentropy     "unistd.h;sys/random.h"  HAVE_GETENTROPY)
  check_symbol_exists(getrandom      "sys/random.h"           HAVE_GETRANDOM)
  check_symbol_exists(posix_memalign "stdlib.h"               HAVE_POSIX_MEMALIGN)
  check_symbol_exists(pselect        "sys/select.h"           HAVE_PSELECT)
  check_symbol_exists(pledge         "unistd.h"               HAVE_PLEDGE)
  check_symbol_exists(mach_absolute_time "mach/mach_time.h"   HAVE_MACH_ABSOLUTE_TIME)

  # Two independent questions, exactly as configure.ac asks them:
  #
  #   FORKPTY_IN_LIBUTIL -- "is <libutil.h> the header that declares forkpty?"
  #                         (true on FreeBSD/macOS, false on glibc where it is
  #                         <pty.h>). Despite the name this is about the
  #                         HEADER, not the library.
  #   HAVE_FORKPTY       -- AC_SEARCH_LIBS: which library, if any, provides it.
  check_cxx_source_compiles("
    #include <sys/types.h>
    #include <libutil.h>
    int main(){ (void) forkpty; return 0; }" FORKPTY_IN_LIBUTIL)

  set(MOSH_FORKPTY_LIB "")
  foreach(_lib "" util bsd)
    if(HAVE_FORKPTY)
      break()
    endif()
    set(CMAKE_REQUIRED_LIBRARIES ${_lib})
    # Unique cache variable per attempt, or CMake reuses the first answer.
    if(_lib STREQUAL "")
      set(_probe _forkpty_libc)
    else()
      set(_probe _forkpty_lib${_lib})
    endif()
    # The probe must CALL forkpty, not merely name it. A discarded-value
    # expression like `(void) forkpty;` emits no relocation, so the link
    # succeeds even when the symbol is nowhere to be found -- the first
    # iteration would then always "succeed" and MOSH_FORKPTY_LIB would stay
    # empty. That goes unnoticed on glibc >= 2.34 (libutil was merged into
    # libc) and breaks the link on older glibc and on the BSDs.
    check_cxx_source_compiles("
      #if defined(HAVE_PTY_H) || __has_include(<pty.h>)
      #include <pty.h>
      #endif
      #if __has_include(<util.h>)
      #include <util.h>
      #endif
      #if __has_include(<libutil.h>)
      #include <sys/types.h>
      #include <libutil.h>
      #endif
      int main(){ return forkpty(0, 0, 0, 0); }" ${_probe})
    set(CMAKE_REQUIRED_LIBRARIES)
    if(${_probe})
      set(HAVE_FORKPTY 1)
      set(MOSH_FORKPTY_LIB "${_lib}")
    endif()
  endforeach()

  # configure.ac:228-244 -- utmp/wtmp records, so `who` and `w` see mosh
  # sessions. Without this mosh-server silently stops writing them.
  include(CheckLibraryExists)
  check_library_exists(utempter utempter_remove_record "" HAVE_UTEMPTER)
  if(HAVE_UTEMPTER)
    set(MOSH_UTEMPTER_LIB utempter)
  else()
    set(MOSH_UTEMPTER_LIB "")
  endif()

  # configure.ac:256-259 -- Solaris/illumos put the sockets API in -lsocket
  # /-lnsl, and glibc < 2.17 puts clock_gettime in -lrt.
  set(MOSH_EXTRA_LIBS "")
  foreach(_l socket nsl rt)
    find_library(_moshlib_${_l} NAMES ${_l})
    if(_moshlib_${_l})
      list(APPEND MOSH_EXTRA_LIBS ${_l})
    endif()
  endforeach()
endif()

# ---------------------------------------------------------------- syslog ---
# configure.ac only looks for syslog.h when --enable-syslog is passed, and that
# defaults to "no". Match that, or mosh-server picks up connection logging that
# the autotools build would not have enabled.
option(MOSH_ENABLE_SYSLOG "Log connection information from mosh-server to syslog" OFF)
if(MOSH_ENABLE_SYSLOG AND NOT WIN32)
  check_include_file(syslog.h HAVE_SYSLOG_H)
  if(HAVE_SYSLOG_H)
    set(HAVE_SYSLOG 1)
  else()
    message(FATAL_ERROR "MOSH_ENABLE_SYSLOG was requested but syslog.h was not found")
  endif()
endif()

# ----------------------------------------------------------- declarations --
# AC_CHECK_DECLS semantics: always defined, to 1 or 0.
# The probe mirrors autoconf's: guard with #ifndef so a macro definition counts
# as "declared", and use a plain discarded-value expression so that functions
# (which have no sizeof) are handled too.
function(_mosh_check_decl name headers out)
  string(REPLACE ";" "\n#include <" _inc "${headers}")
  check_cxx_source_compiles("
    #include <${_inc}>
    int main(){
    #ifndef ${name}
      (void) ${name};
    #endif
      return 0; }" ${out}_RAW)
  if(${out}_RAW)
    set(${out} 1 PARENT_SCOPE)
  else()
    set(${out} 0 PARENT_SCOPE)
  endif()
endfunction()

if(WIN32)
  set(HAVE_DECL_BE64TOH 0)
  set(HAVE_DECL_BETOH64 0)
  set(HAVE_DECL_BSWAP64 0)
  set(HAVE_DECL_FFS 0)
  set(HAVE_DECL___BUILTIN_BSWAP64 0)
  set(HAVE_DECL___BUILTIN_CTZ 0)
else()
  _mosh_check_decl(be64toh "endian.h" HAVE_DECL_BE64TOH)
  _mosh_check_decl(betoh64 "endian.h" HAVE_DECL_BETOH64)
  _mosh_check_decl(bswap64 "sys/endian.h" HAVE_DECL_BSWAP64)
  _mosh_check_decl(ffs "strings.h" HAVE_DECL_FFS)
  check_cxx_source_compiles("int main(){ return (int)__builtin_bswap64(1ULL); }"
                            _bswap64_builtin)
  if(_bswap64_builtin)
    set(HAVE_DECL___BUILTIN_BSWAP64 1)
  else()
    set(HAVE_DECL___BUILTIN_BSWAP64 0)
  endif()
  check_cxx_source_compiles("int main(){ return __builtin_ctz(1u); }"
                            _ctz_builtin)
  if(_ctz_builtin)
    set(HAVE_DECL___BUILTIN_CTZ 1)
  else()
    set(HAVE_DECL___BUILTIN_CTZ 0)
  endif()
endif()

# ---------------------------------------------------------------- symbols --
if(NOT WIN32)
  # IUTF8 needs the GNU/BSD feature macros the terminal code compiles with.
  set(CMAKE_REQUIRED_DEFINITIONS -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600)
  check_symbol_exists(IUTF8 "termios.h" HAVE_IUTF8)
  set(CMAKE_REQUIRED_DEFINITIONS)

  check_symbol_exists(IP_MTU_DISCOVER "netinet/in.h" HAVE_IP_MTU_DISCOVER)
  check_symbol_exists(IP_RECVTOS      "netinet/in.h" HAVE_IP_RECVTOS)

  # FD_ISSET takes a const fd_set* on Linux but not everywhere; the Select
  # wrapper's read() is declared const only when this holds.
  check_cxx_source_compiles("
    #include <sys/select.h>
    int main(){ const fd_set s{}; return FD_ISSET(0, &s); }" FD_ISSET_IS_CONST)
endif()

check_type_size(uintptr_t SIZEOF_UINTPTR_T)
if(HAVE_SIZEOF_UINTPTR_T)
  set(HAVE_UINTPTR_T 1)
endif()

# ------------------------------------------------------------------ misc ---
if(MSVC)
  set(MOSH_RESTRICT "__restrict")
else()
  set(MOSH_RESTRICT "__restrict__")
endif()

if(WIN32)
  set(MOSH_PLATFORM_WIN32 1)
else()
  set(MOSH_PLATFORM_POSIX 1)
endif()
