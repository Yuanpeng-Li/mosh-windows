# Mirrors src/crypto/Makefile.am's USE_AES_OCB_FROM_OPENSSL conditional.
if(MOSH_USE_OPENSSL_OCB)
  set(_ocb_src ocb_openssl.cc)
else()
  set(_ocb_src ocb_internal.cc)
endif()

add_library(moshcrypto STATIC
  ${_ocb_src}
  ae.h
  base64.cc base64.h
  byteorder.h
  crypto.cc crypto.h
  prng.h)

target_link_libraries(moshcrypto PUBLIC mosh_common OpenSSL::Crypto)

if(WIN32)
  # BCryptGenRandom, the entropy source in src/crypto/prng.h.
  target_link_libraries(moshcrypto PUBLIC bcrypt)
endif()
add_dependencies(moshcrypto mosh_version)
