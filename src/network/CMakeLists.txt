add_library(moshnetwork STATIC
  network.cc              network.h
  networktransport.h      networktransport-impl.h
  transportfragment.cc    transportfragment.h
  transportsender.h       transportsender-impl.h
  transportstate.h
  compressor.cc           compressor.h)

target_link_libraries(moshnetwork PUBLIC
  mosh_common
  moshutil
  moshcrypto
  moshprotos
  ZLIB::ZLIB)
add_dependencies(moshnetwork mosh_version)
