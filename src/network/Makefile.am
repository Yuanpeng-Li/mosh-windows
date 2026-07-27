AM_CPPFLAGS = -I$(top_srcdir)/ $(protobuf_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libmoshnetwork.a

libmoshnetwork_a_SOURCES = socketio.h socketio_posix.cc network.cc network.h networktransport-impl.h networktransport.h transportfragment.cc transportfragment.h transportsender-impl.h transportsender.h transportstate.h compressor.cc compressor.h

# Ship the peer CMake build in the dist tarball.
EXTRA_DIST = CMakeLists.txt
EXTRA_DIST += socketio_win32.cc
