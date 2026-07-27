AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libmoshstatesync.a

libmoshstatesync_a_SOURCES = completeterminal.cc completeterminal.h user.cc user.h

# Ship the peer CMake build in the dist tarball.
EXTRA_DIST = CMakeLists.txt
