AM_CXXFLAGS = -I$(top_srcdir)/ $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libmoshutil.a

libmoshutil_a_SOURCES = utf8.cc utf8.h uniwidth.cc uniwidth.h uniwidth_table.h locale_utils.cc locale_utils.h swrite.cc swrite.h dos_assert.h fatal_assert.h select.h select.cc timestamp.h timestamp.cc pty_compat.cc pty_compat.h

# Ship the peer CMake build in the dist tarball.
EXTRA_DIST = CMakeLists.txt
