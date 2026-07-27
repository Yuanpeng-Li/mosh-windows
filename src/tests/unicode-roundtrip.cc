/*
    Mosh: the mobile shell
    Copyright 2012 Keith Winstein

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.
*/

/* End-to-end character round trip: bytes -> UTF8Parser -> Emulator ->
 * Framebuffer, and back out as UTF-8.
 *
 * This is the test that has teeth on Windows. The rest of the suite is shell
 * scripts driving tmux and a pty, none of which runs there; and running it on
 * Linux proves little about the thing it is guarding, because wchar_t is
 * already 32 bits on Linux, so the bug it catches cannot occur.
 *
 * The bug: mosh stored one code point per wchar_t. MSVC's wchar_t is 16 bits,
 * so U+1F600 became U+F600, and wcrtomb() returned (size_t)-1 for the lone
 * surrogate that produced -- a value the framebuffer then used as a pointer
 * offset. The first emoji could corrupt the heap rather than merely render
 * wrongly. Astral code points are therefore the point of this test, and it
 * must be run on the Windows build.
 */

#include "src/include/config.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "src/terminal/parser.h"
#include "src/terminal/terminal.h"

static int failures = 0;

static void check( bool ok, const char* what, const std::string& got, const std::string& want )
{
  if ( ok ) {
    return;
  }
  failures++;
  fprintf( stderr, "FAIL: %s\n", what );
  fprintf( stderr, "  got  :" );
  for ( size_t i = 0; i < got.size(); i++ ) {
    fprintf( stderr, " %02x", (unsigned char)got[i] );
  }
  fprintf( stderr, "\n  want :" );
  for ( size_t i = 0; i < want.size(); i++ ) {
    fprintf( stderr, " %02x", (unsigned char)want[i] );
  }
  fprintf( stderr, "\n" );
}

/* Feed bytes to a fresh emulator and read back what landed on the first row. */
static std::string render( const std::string& input, int width = 20 )
{
  Terminal::Emulator terminal( width, 3 );
  Parser::UTF8Parser parser;
  Parser::Actions actions;

  for ( size_t i = 0; i < input.size(); i++ ) {
    actions.clear();
    parser.input( input[i], actions );
    for ( Parser::Actions::iterator it = actions.begin(); it != actions.end(); it++ ) {
      ( *it )->act_on_terminal( &terminal );
    }
  }

  std::string out;
  const Terminal::Framebuffer& fb = terminal.get_fb();
  for ( int col = 0; col < width; col++ ) {
    const Terminal::Cell* cell = fb.get_cell( 0, col );
    if ( cell->empty() ) {
      continue;
    }
    cell->print_grapheme( out );
  }
  /* Trailing blanks from the second half of a wide cell are not content. */
  while ( !out.empty() && out[out.size() - 1] == ' ' ) {
    out.erase( out.size() - 1 );
  }
  return out;
}

int main( void )
{
  /* --- characters that must survive unchanged ------------------------- */
  struct
  {
    const char* utf8;
    const char* what;
  } survives[] = {
    { "A", "U+0041" },
    { "\xC3\xA9", "U+00E9 e-acute" },
    { "\xE4\xB8\xAD", "U+4E2D CJK, wide" },
    { "\xE3\x81\x82", "U+3042 hiragana, wide" },
    { "\xEF\xBC\xA1", "U+FF21 fullwidth A, wide" },
    { "\xF0\x9F\x98\x80", "U+1F600 GRINNING FACE -- astral" },
    { "\xF0\xA0\x80\x80", "U+20000 CJK ext B -- astral" },
    /* Astral and narrow, so both width classes above the BMP are covered.
       Not U+10FFFF: that is a noncharacter, width -1, and correctly dropped. */
    { "\xF0\x9D\x84\x9E", "U+1D11E MUSICAL SYMBOL G CLEF -- astral, narrow" },
    { "\xF4\x8F\xBF\xBD", "U+10FFFD -- astral private use, last printable plane" },
    { "\xEF\xBF\xBD", "U+FFFD itself" },
    { "\xE4\xB8\xAD\xE6\x96\x87", "two wide characters" },
    { "A\xF0\x9F\x98\x80\x42", "astral between two ASCII" },
  };

  for ( size_t i = 0; i < sizeof survives / sizeof *survives; i++ ) {
    const std::string in( survives[i].utf8 );
    const std::string out = render( in );
    check( out == in, survives[i].what, out, in );
  }

  /* --- malformed input becomes U+FFFD, and does not crash -------------- */
  const std::string FFFD = "\xEF\xBF\xBD";
  struct
  {
    const char* bytes;
    size_t len;
    const char* what;
  } mangled[] = {
    { "\xED\xA0\x80", 3, "lone surrogate U+D800" },
    { "\xED\xBF\xBF", 3, "lone surrogate U+DFFF" },
    { "\xC0\xAF", 2, "overlong solidus" },
    { "\xE0\x80\xAF", 3, "overlong three-byte" },
    { "\xF0\x80\x80\xAF", 4, "overlong four-byte" },
    { "\xFE", 1, "invalid lead byte" },
    { "\xFF", 1, "invalid lead byte" },
    { "\x80", 1, "bare continuation" },
  };
  for ( size_t i = 0; i < sizeof mangled / sizeof *mangled; i++ ) {
    const std::string out = render( std::string( mangled[i].bytes, mangled[i].len ) );
    /* What matters is that it is replaced and the process survives; how many
       U+FFFDs a given malformed run produces is the parser's business. */
    const bool ok = !out.empty() && out.find( FFFD ) != std::string::npos;
    check( ok, mangled[i].what, out, FFFD );
  }

  /* A truncated sequence must simply leave nothing behind, not crash. */
  {
    const std::string out = render( "\xF0\x9F\x98" ); /* first three bytes of U+1F600 */
    check( out.empty() || out == FFFD, "truncated astral sequence", out, std::string( "(nothing)" ) );
  }

  /* --- the window title, which is stored as code points, not bytes ----- */
  {
    /* OSC 0 ; <title> BEL */
    const std::string title = "\xE4\xB8\xAD\xF0\x9F\x98\x80";
    const std::string seq = std::string( "\x1b]0;" ) + title + "\x07";

    Terminal::Emulator terminal( 20, 3 );
    Parser::UTF8Parser parser;
    Parser::Actions actions;
    for ( size_t i = 0; i < seq.size(); i++ ) {
      actions.clear();
      parser.input( seq[i], actions );
      for ( Parser::Actions::iterator it = actions.begin(); it != actions.end(); it++ ) {
        ( *it )->act_on_terminal( &terminal );
      }
    }

    const Terminal::Framebuffer::title_type& t = terminal.get_fb().get_window_title();
    std::string back;
    for ( size_t i = 0; i < t.size(); i++ ) {
      Terminal::Cell::append_to_str( back, t[i] );
    }
    check( back == title, "window title round trip, including an astral code point", back, title );
  }

  printf( "unicode-roundtrip: %d failures\n", failures );
  return failures ? 1 : 0;
}
