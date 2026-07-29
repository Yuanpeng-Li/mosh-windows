# Link order mirrors LDADD in src/frontend/Makefile.am. With CMake targets the
# order is resolved automatically from the dependency graph, so we just name
# the direct dependencies.

set(_mosh_link_libs
  moshcrypto
  moshnetwork
  moshstatesync
  moshterminal
  moshutil
  moshprotos)

if(MOSH_BUILD_CLIENT)
  add_executable(mosh-client
    mosh-client.cc
    stmclient.cc      stmclient.h
    terminaloverlay.cc terminaloverlay.h)
  target_link_libraries(mosh-client PRIVATE mosh_common ${_mosh_link_libs})
  add_dependencies(mosh-client mosh_version)
  if(NOT WIN32)
    target_compile_definitions(mosh-client PRIVATE _DEFAULT_SOURCE _XOPEN_SOURCE=600)
    target_link_libraries(mosh-client PRIVATE m)
  endif()
  install(TARGETS mosh-client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(MOSH_BUILD_SERVER)
  set(_mosh_server_sources mosh-server.cc)
  if(WIN32)
    # What fork() does on POSIX: see the header for why it cannot be one
    # portable function.
    list(APPEND _mosh_server_sources win32detach.cc win32detach.h)
  endif()
  add_executable(mosh-server ${_mosh_server_sources})
  target_link_libraries(mosh-server PRIVATE mosh_common ${_mosh_link_libs})
  add_dependencies(mosh-server mosh_version)
  if(NOT WIN32)
    target_compile_definitions(mosh-server PRIVATE _DEFAULT_SOURCE _XOPEN_SOURCE=600)
    target_link_libraries(mosh-server PRIVATE m)
    if(MOSH_UTEMPTER_LIB)
      # Without this mosh-server stops writing utmp/wtmp records and `who`/`w`
      # no longer show mosh sessions -- a silent functional regression.
      target_link_libraries(mosh-server PRIVATE ${MOSH_UTEMPTER_LIB})
    endif()
  endif()
  install(TARGETS mosh-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Hardening link flags were probed at the top level; applying an unprobed
# -Wl,-z,* here would break the Apple linker.
if(MOSH_HARDEN AND MOSH_HARDEN_LINK_FLAGS)
  foreach(_t mosh-client mosh-server)
    if(TARGET ${_t})
      target_link_options(${_t} PRIVATE ${MOSH_HARDEN_LINK_FLAGS})
    endif()
  endforeach()
endif()

# Solaris/illumos (-lsocket -lnsl) and glibc < 2.17 (-lrt for clock_gettime).
if(MOSH_EXTRA_LIBS)
  foreach(_t mosh-client mosh-server)
    if(TARGET ${_t})
      target_link_libraries(${_t} PRIVATE ${MOSH_EXTRA_LIBS})
    endif()
  endforeach()
endif()
