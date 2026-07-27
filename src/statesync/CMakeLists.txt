add_library(moshstatesync STATIC
  completeterminal.cc completeterminal.h
  user.cc             user.h)

target_link_libraries(moshstatesync PUBLIC
  mosh_common
  moshterminal
  moshprotos)
add_dependencies(moshstatesync mosh_version)
