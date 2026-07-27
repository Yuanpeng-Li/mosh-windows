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

#include "src/util/utf8.h"

namespace Util {

size_t utf8_decode( char32_t* pwc, const char* s, size_t n )
{
  if ( n == 0 ) {
    return UTF8_INCOMPLETE;
  }

  const unsigned char b0 = static_cast<unsigned char>( s[0] );

  /* ASCII, including U+0000. */
  if ( b0 <= 0x7F ) {
    *pwc = b0;
    return b0 == 0 ? 0 : 1;
  }

  /* A continuation byte cannot start a character. */
  if ( b0 < 0xC2 ) {
    /* 0x80..0xBF are continuations; 0xC0 and 0xC1 could only ever introduce
       an overlong encoding of an ASCII character. */
    return UTF8_INVALID;
  }

  size_t len;
  char32_t wc;
  char32_t min;
  if ( b0 <= 0xDF ) {
    len = 2;
    wc = b0 & 0x1F;
    min = 0x80;
  } else if ( b0 <= 0xEF ) {
    len = 3;
    wc = b0 & 0x0F;
    min = 0x800;
  } else if ( b0 <= 0xF7 ) {
    len = 4;
    wc = b0 & 0x07;
    min = 0x10000;
  } else if ( b0 <= 0xFB ) {
    len = 5;
    wc = b0 & 0x03;
    min = 0x200000;
  } else if ( b0 <= 0xFD ) {
    len = 6;
    wc = b0 & 0x01;
    min = 0x4000000;
  } else {
    return UTF8_INVALID; /* 0xFE, 0xFF are not lead bytes in any encoding */
  }

  /* Validation happens after the character is assembled, not at the second
     byte. Unicode 6.0 section 3.9 recommends rejecting the "maximal subpart"
     as early as possible, but glibc's mbrtowc does not: it reports E0 80 as
     incomplete rather than invalid, and only rejects once it has the whole
     character. Matching glibc is what matters here, not the recommendation --
     a client and server that disagree emit a different number of U+FFFDs for
     the same malformed input, which desynchronises the screen. */
  for ( size_t i = 1; i < len; i++ ) {
    if ( i >= n ) {
      return UTF8_INCOMPLETE;
    }
    const unsigned char b = static_cast<unsigned char>( s[i] );
    if ( ( b & 0xC0 ) != 0x80 ) {
      return UTF8_INVALID;
    }
    wc = ( wc << 6 ) | ( b & 0x3F );
  }

  if ( wc < min ) {
    return UTF8_INVALID; /* overlong encoding */
  }
  if ( wc >= 0xD800 && wc <= 0xDFFF ) {
    return UTF8_INVALID; /* surrogate */
  }

  *pwc = wc;
  return len;
}

size_t utf8_encode( char out[UTF8_MAX_LEN], char32_t wc )
{
  if ( !utf8_encodable( wc ) ) {
    wc = UTF8_REPLACEMENT;
  }

  size_t len;
  unsigned char lead;
  if ( wc <= 0x7F ) {
    out[0] = static_cast<char>( wc );
    return 1;
  } else if ( wc <= 0x7FF ) {
    len = 2;
    lead = 0xC0;
  } else if ( wc <= 0xFFFF ) {
    len = 3;
    lead = 0xE0;
  } else if ( wc <= 0x1FFFFF ) {
    len = 4;
    lead = 0xF0;
  } else if ( wc <= 0x3FFFFFF ) {
    len = 5;
    lead = 0xF8;
  } else {
    len = 6;
    lead = 0xFC;
  }

  for ( size_t i = len - 1; i > 0; i-- ) {
    out[i] = static_cast<char>( 0x80 | ( wc & 0x3F ) );
    wc >>= 6;
  }
  out[0] = static_cast<char>( lead | wc );
  return len;
}

}
