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

#ifndef UTF8_HPP
#define UTF8_HPP

#include <cstddef>
#include <cstdint>
#include <string>

/* A UTF-8 codec that does not depend on the process locale.
 *
 * mosh used mbrtowc()/wcrtomb() for this, which has two problems. It only
 * works in a UTF-8 locale, so correctness depends on the environment rather
 * than on the code; and wchar_t is 16 bits on Windows, where a code point
 * above U+FFFF cannot be represented at all -- wcrtomb() then returns
 * (size_t)-1 for the resulting lone surrogate, which the caller adds to a
 * pointer.
 *
 * decode() deliberately mirrors mbrtowc()'s return contract so it is a drop-in
 * replacement, including the (size_t)-1 / (size_t)-2 distinction the parser
 * relies on to implement Unicode 6.0 section 3.9's U+FFFD recommendations.
 */

namespace Util {

static const char32_t UTF8_REPLACEMENT = 0xFFFD;

/* The accepted range is glibc's, not Unicode's.
 *
 * RFC 3629 capped UTF-8 at four bytes and U+10FFFF in 2003, but glibc's
 * mbrtowc still decodes the original five- and six-byte forms up to
 * U+7FFFFFFF -- measured on glibc 2.41: FD BF BF BF BF BF yields U+7FFFFFFF,
 * and F5 80 80 80 yields U+140000 rather than an error. mosh has always
 * inherited that, and such code points end up with width -1 and are dropped as
 * unprintable. Rejecting them here instead would turn a silently dropped
 * character into a visible U+FFFD, which is a behaviour change, so the wider
 * range is deliberate. Overlong encodings and surrogates are still rejected,
 * as glibc rejects them.
 */
static const char32_t UTF8_MAX_CODE_POINT = 0x7FFFFFFF;
static const char32_t UTF8_MAX_UNICODE = 0x10FFFF;
static const size_t UTF8_MAX_LEN = 6;

/* Return codes matching mbrtowc(). */
static const size_t UTF8_INVALID = static_cast<size_t>( -1 );
static const size_t UTF8_INCOMPLETE = static_cast<size_t>( -2 );

/* A Unicode scalar value: within Unicode's range and not a surrogate. */
inline bool utf8_is_scalar( char32_t wc )
{
  return wc <= UTF8_MAX_UNICODE && !( wc >= 0xD800 && wc <= 0xDFFF );
}

/* What this codec will round-trip: the wider glibc range, minus surrogates. */
inline bool utf8_encodable( char32_t wc )
{
  return wc <= UTF8_MAX_CODE_POINT && !( wc >= 0xD800 && wc <= 0xDFFF );
}

/* Decode one character from s[0..n).
 *
 * Contract, identical to mbrtowc() in a UTF-8 locale:
 *   0                -- decoded U+0000; *pwc is set to 0
 *   1..4             -- that many bytes consumed; *pwc is set
 *   UTF8_INCOMPLETE  -- s[0..n) is a valid prefix but the character is not
 *                       complete; feed more bytes
 *   UTF8_INVALID     -- s[0..n) cannot begin a valid character
 *
 * Rejects, as glibc does: continuation bytes in the lead position, overlong
 * encodings, surrogates (U+D800..U+DFFF), and anything above U+10FFFF. Never
 * sets *pwc on a failure return.
 */
size_t utf8_decode( char32_t* pwc, const char* s, size_t n );

/* Encode wc into out[0..4). Returns the number of bytes written, 1 to 4.
 *
 * Unlike wcrtomb() this cannot fail: a non-scalar argument is encoded as
 * U+FFFD rather than returning an error the caller might add to a pointer.
 */
size_t utf8_encode( char out[4], char32_t wc );

/* Whole-string conversions, for text that crosses between the byte world and
   the code-point world -- mosh's own status messages, which are formatted as
   bytes and then drawn one character at a time.

   Malformed input becomes U+FFFD rather than an error: these are diagnostics,
   and failing to render "connection lost" because it arrived mis-encoded would
   be the worse outcome. */
std::u32string utf8_to_u32( const std::string& in );
std::string u32_to_utf8( const std::u32string& in );

}

#endif
