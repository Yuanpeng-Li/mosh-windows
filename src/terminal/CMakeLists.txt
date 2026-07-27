add_library(moshterminal STATIC
  parseraction.cc      parseraction.h
  parser.cc            parser.h
  parserstate.cc       parserstate.h
  parserstatefamily.h
  parsertransition.h
  terminal.cc          terminal.h
  terminaldispatcher.cc terminaldispatcher.h
  terminaldisplay.cc   terminaldisplay.h
  terminaldisplayinit.cc
  terminalframebuffer.cc terminalframebuffer.h
  terminalfunctions.cc
  terminaluserinput.cc terminaluserinput.h)

target_link_libraries(moshterminal PUBLIC mosh_common moshutil)
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
