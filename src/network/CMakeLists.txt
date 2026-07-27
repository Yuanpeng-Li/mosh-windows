set(_moshnetwork_sources
  network.cc              network.h
  socketio.h
  networktransport.h      networktransport-impl.h
  transportfragment.cc    transportfragment.h
  transportsender.h       transportsender-impl.h
  transportstate.h
  compressor.cc           compressor.h)

if(WIN32)
  list(APPEND _moshnetwork_sources socketio_win32.cc)
else()
  list(APPEND _moshnetwork_sources socketio_posix.cc)
endif()

add_library(moshnetwork STATIC ${_moshnetwork_sources})

target_link_libraries(moshnetwork PUBLIC
  mosh_common
  moshutil
  moshcrypto
  moshprotos
  ZLIB::ZLIB)
add_dependencies(moshnetwork mosh_version)
