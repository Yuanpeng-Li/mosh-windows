AM_CPPFLAGS = -I$(top_srcdir)/ $(TINFO_CFLAGS)
AM_CXXFLAGS = $(WARNING_CXXFLAGS) $(PICKY_CXXFLAGS) $(HARDEN_CFLAGS) $(MISC_CXXFLAGS) $(CODE_COVERAGE_CXXFLAGS)

noinst_LIBRARIES = libmoshterminal.a

libmoshterminal_a_SOURCES = parseraction.cc parseraction.h parser.cc parser.h parserstate.cc parserstatefamily.h parserstate.h parsertransition.h terminal.cc terminaldispatcher.cc terminaldispatcher.h terminaldisplay.cc terminaldisplayinit.cc terminaldisplay.h terminalframebuffer.cc terminalframebuffer.h terminalfunctions.cc terminal.h terminaluserinput.cc terminaluserinput.h

# Ship the peer CMake build, and the Windows-only sources it selects, in the
# dist tarball. terminaldisplayinit_win32.cc replaces the terminfo probe where
# there is no terminfo.
EXTRA_DIST = CMakeLists.txt terminaldisplayinit_win32.cc
