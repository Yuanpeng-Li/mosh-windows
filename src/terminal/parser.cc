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

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations including
    the two.

    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the
    file(s), but you are not obligated to do so. If you do not wish to do
    so, delete this exception statement from your version. If you delete
    this exception statement from all source files in the program, then
    also delete it here.
*/

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <typeinfo>

#include "src/util/utf8.h"

#include "src/terminal/parser.h"

const Parser::StateFamily Parser::family;

static void append_or_delete( Parser::ActionPointer act, Parser::Actions& vec )
{
  assert( act );

  if ( !act->ignore() ) {
    vec.push_back( act );
  }
}

void Parser::Parser::input( char32_t ch, Actions& ret )
{
  Transition tx = state->input( ch );

  if ( tx.next_state != NULL ) {
    append_or_delete( state->exit(), ret );
  }

  append_or_delete( tx.action, ret );

  if ( tx.next_state != NULL ) {
    append_or_delete( tx.next_state->enter(), ret );
    state = tx.next_state;
  }
}

Parser::UTF8Parser::UTF8Parser() : parser(), buf_len( 0 )
{
  assert( BUF_SIZE >= Util::UTF8_MAX_LEN );
  buf[0] = '\0';
}

void Parser::UTF8Parser::input( char c, Actions& ret )
{
  assert( buf_len < BUF_SIZE );

  /* 1-byte UTF-8 character, aka ASCII?  Cheat. */
  if ( buf_len == 0 && static_cast<unsigned char>( c ) <= 0x7f ) {
    parser.input( static_cast<char32_t>( c ), ret );
    return;
  }

  buf[buf_len++] = c;

  /* Decoded with mosh's own codec rather than mbrtowc: this no longer depends
     on the process locale being UTF-8, and it works where wchar_t is too
     narrow to hold a code point. Util::utf8_decode keeps mbrtowc's return
     contract, so the recovery logic below is unchanged. */
  char32_t pwc;

  size_t total_bytes_parsed = 0;
  size_t orig_buf_len = buf_len;

  /* this routine is somewhat complicated in order to comply with
     Unicode 6.0, section 3.9, "Best Practices for using U+FFFD" */

  while ( total_bytes_parsed != orig_buf_len ) {
    assert( total_bytes_parsed < orig_buf_len );
    assert( buf_len > 0 );
    size_t bytes_parsed = Util::utf8_decode( &pwc, buf, buf_len );

    /* this returns 0 when n = 0! */

    if ( bytes_parsed == 0 ) {
      /* character was NUL, accept and clear buffer */
      assert( buf_len == 1 );
      buf_len = 0;
      pwc = 0;
      bytes_parsed = 1;
    } else if ( bytes_parsed == Util::UTF8_INVALID ) {
      /* invalid sequence, use replacement character and try again with last char */
      if ( buf_len > 1 ) {
        buf[0] = buf[buf_len - 1];
        bytes_parsed = buf_len - 1;
        buf_len = 1;
      } else {
        buf_len = 0;
        bytes_parsed = 1;
      }
      pwc = Util::UTF8_REPLACEMENT;
    } else if ( bytes_parsed == Util::UTF8_INCOMPLETE ) {
      /* can't parse incomplete multibyte character */
      total_bytes_parsed += buf_len;
      continue;
    } else {
      /* parsed into pwc, accept */
      assert( bytes_parsed <= buf_len );
      memmove( buf, buf + bytes_parsed, buf_len - bytes_parsed );
      buf_len = buf_len - bytes_parsed;
    }

    /* The decoder accepts the range glibc's mbrtowc accepts, which reaches
       past Unicode (see src/util/utf8.h); clamp what the terminal sees. It
       rejects surrogates outright, but keep the check -- some C libraries let
       them through, and they must not reach the user's terminal. */
    if ( pwc > 0x10FFFF ) {
      pwc = Util::UTF8_REPLACEMENT;
    }
    if ( ( pwc >= 0xD800 ) && ( pwc <= 0xDFFF ) ) {
      pwc = Util::UTF8_REPLACEMENT;
    }

    parser.input( pwc, ret );

    total_bytes_parsed += bytes_parsed;
  }
}

Parser::Parser::Parser( const Parser& other ) : state( other.state ) {}

Parser::Parser& Parser::Parser::operator=( const Parser& other )
{
  state = other.state;
  return *this;
}
