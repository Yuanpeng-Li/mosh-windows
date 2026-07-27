# terminaldisplayinit.cc is the terminfo probe, and there is no terminfo on
# Windows; it is replaced by a compiled-in capability table as part of the
# client port. Excluding it here lets everything else -- the parser, the
# emulator and the framebuffer -- build and be tested under MSVC first.
set(_moshterminal_sources
  parseraction.cc      parseraction.h
  parser.cc            parser.h
  parserstate.cc       parserstate.h
  parserstatefamily.h
  parsertransition.h
  terminal.cc          terminal.h
  terminaldispatcher.cc terminaldispatcher.h
  terminaldisplay.cc   terminaldisplay.h
  terminalframebuffer.cc terminalframebuffer.h
  terminalfunctions.cc
  terminaluserinput.cc terminaluserinput.h)

if(NOT WIN32)
  list(APPEND _moshterminal_sources terminaldisplayinit.cc)
endif()

add_library(moshterminal STATIC ${_moshterminal_sources})

target_link_libraries(moshterminal PUBLIC mosh_common moshutil_portable)
add_dependencies(moshterminal mosh_version)

if(NOT WIN32)
  # configure.ac passes these with TINFO_CFLAGS; terminaldisplayinit.cc needs
  # them for the terminfo entry points.
  target_compile_definitions(moshterminal PRIVATE _DEFAULT_SOURCE _XOPEN_SOURCE=600)
  target_link_libraries(moshterminal PUBLIC ${MOSH_TINFO_LIBS})
  if(MOSH_TINFO_INCLUDE_DIRS)
    target_include_directories(moshterminal PUBLIC ${MOSH_TINFO_INCLUDE_DIRS})
  endif()
  if(MOSH_TINFO_LIBRARY_DIRS)
    target_link_directories(moshterminal PUBLIC ${MOSH_TINFO_LIBRARY_DIRS})
  endif()
endif()
