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

/* Differential test for src/util/utf8.
 *
 * The codec exists to replace mbrtowc()/wcrtomb(), so the test is: does it
 * agree with them? Where a UTF-8 locale is available the comparison is run
 * exhaustively over every scalar value and every one- and two-byte sequence,
 * which covers the overlong, surrogate and out-of-range rejections. Where it
 * is not -- notably MSVC, where wchar_t is 16 bits and cannot hold an astral
 * code point at all -- the self-consistency and known-vector parts still run,
 * and those are what matter on that platform.
 */

#include "src/include/config.h"

#include <climits>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

#include "src/util/utf8.h"

static int failures = 0;
static int checks = 0;

static void fail( const char* what, const char* detail )
{
  fprintf( stderr, "FAIL: %s: %s\n", what, detail );
  if ( ++failures > 20 ) {
    fprintf( stderr, "too many failures, stopping\n" );
    exit( 1 );
  }
}

static std::string hex( const char* s, size_t n )
{
  std::string out;
  char b[8];
  for ( size_t i = 0; i < n; i++ ) {
    snprintf( b, sizeof b, "%02x ", (unsigned char)s[i] );
    out += b;
  }
  return out;
}

/* ------------------------------------------------- self-consistency ------ */

static void test_roundtrip( void )
{
  /* The whole accepted range, which is wider than Unicode -- see utf8.h.
     Stepping thins out the far end so the test stays quick. */
  for ( char32_t wc = 0;; wc += ( wc < 0x110000 ? 1 : 0x3FF ) ) {
    if ( wc > Util::UTF8_MAX_CODE_POINT ) {
      break;
    }
    if ( wc >= 0xD800 && wc <= 0xDFFF ) {
      continue; /* surrogates are never encoded */
    }
    char buf[Util::UTF8_MAX_LEN];
    size_t n = Util::utf8_encode( buf, wc );
    checks++;
    if ( n < 1 || n > Util::UTF8_MAX_LEN ) {
      fail( "encode length", "out of range" );
      continue;
    }
    char32_t back = 0xFFFFFFFF;
    size_t got = Util::utf8_decode( &back, buf, n );
    if ( got != ( wc == 0 ? 0u : n ) || back != wc ) {
      char d[128];
      snprintf( d, sizeof d, "U+%04X encoded as %s decoded to U+%04X (ret %lld)", (unsigned)wc,
                hex( buf, n ).c_str(), (unsigned)back, (long long)got );
      fail( "round trip", d );
    }
    /* Every proper prefix must report INCOMPLETE, never INVALID: that is what
       lets the parser feed bytes one at a time. */
    for ( size_t k = 1; k < n; k++ ) {
      char32_t tmp;
      if ( Util::utf8_decode( &tmp, buf, k ) != Util::UTF8_INCOMPLETE ) {
        char d[128];
        snprintf( d, sizeof d, "U+%04X prefix of %zu bytes (%s) not INCOMPLETE", (unsigned)wc, k,
                  hex( buf, k ).c_str() );
        fail( "prefix", d );
      }
    }
  }
}

static void test_non_scalars_encode_as_replacement( void )
{
  const char32_t bad[] = { 0xD800, 0xDBFF, 0xDC00, 0xDFFF, 0x80000000, 0xFFFFFFFF };
  char expected[Util::UTF8_MAX_LEN];
  size_t elen = Util::utf8_encode( expected, Util::UTF8_REPLACEMENT );
  for ( size_t i = 0; i < sizeof bad / sizeof *bad; i++ ) {
    char buf[Util::UTF8_MAX_LEN];
    size_t n = Util::utf8_encode( buf, bad[i] );
    checks++;
    if ( n != elen || memcmp( buf, expected, n ) != 0 ) {
      char d[96];
      snprintf( d, sizeof d, "U+%X did not encode as U+FFFD", (unsigned)bad[i] );
      fail( "non-scalar encode", d );
    }
  }
}

/* Sequences that must be rejected, independent of any reference. */
static void test_known_bad( void )
{
  struct
  {
    const char* bytes;
    size_t len;
    const char* why;
  } cases[] = {
    { "\x80", 1, "bare continuation" },
    { "\xBF", 1, "bare continuation" },
    { "\xC0\xAF", 2, "overlong /" },
    { "\xC1\xBF", 2, "overlong DEL" },
    { "\xE0\x80\xAF", 3, "overlong three-byte" },
    { "\xE0\x9F\xBF", 3, "overlong three-byte" },
    { "\xF0\x80\x80\xAF", 4, "overlong four-byte" },
    { "\xF0\x8F\xBF\xBF", 4, "overlong four-byte" },
    { "\xED\xA0\x80", 3, "U+D800 surrogate" },
    { "\xED\xBF\xBF", 3, "U+DFFF surrogate" },
    { "\xFE", 1, "invalid lead" },
    { "\xFF", 1, "invalid lead" },
    { "\xC2\x41", 2, "bad continuation" },
    { "\xE1\x80\x41", 3, "bad continuation" },
  };
  for ( size_t i = 0; i < sizeof cases / sizeof *cases; i++ ) {
    char32_t wc;
    checks++;
    if ( Util::utf8_decode( &wc, cases[i].bytes, cases[i].len ) != Util::UTF8_INVALID ) {
      char d[160];
      snprintf( d, sizeof d, "%s (%s) was accepted", cases[i].why, hex( cases[i].bytes, cases[i].len ).c_str() );
      fail( "known bad", d );
    }
  }
}

/* ------------------------------------------------------ vs the C library -- */

/* Set by test_against_libc before any comparison runs; see
   libc_accepts_extended_forms below for what it means. */
static bool libc_extended = true;

static bool utf8_locale( void )
{
  if ( !setlocale( LC_ALL, "C.UTF-8" ) && !setlocale( LC_ALL, "en_US.UTF-8" ) && !setlocale( LC_ALL, "" ) ) {
    return false;
  }
  /* MB_CUR_MAX of 4 is the practical test for a UTF-8 locale. */
  return MB_CUR_MAX >= 4 && sizeof( wchar_t ) >= 4;
}

static void compare_with_libc( const char* bytes, size_t n )
{
  /* 0xF8..0xFD introduce the five- and six-byte forms. A library that rejects
     them calls such a sequence invalid where we call it incomplete, which is a
     disagreement about a dialect rather than about UTF-8. */
  if ( !libc_extended && n > 0 && (unsigned char)bytes[0] >= 0xF8 ) {
    return;
  }

  char32_t mine = 0;
  size_t mine_ret = Util::utf8_decode( &mine, bytes, n );

  wchar_t theirs = 0;
  mbstate_t ps = mbstate_t();
  size_t their_ret = mbrtowc( &theirs, bytes, n, &ps );

  checks++;

  /* Normalise: both report the same three failure modes. */
  const bool mine_bad = ( mine_ret == Util::UTF8_INVALID );
  const bool their_bad = ( their_ret == (size_t)-1 );
  const bool mine_short = ( mine_ret == Util::UTF8_INCOMPLETE );
  const bool their_short = ( their_ret == (size_t)-2 );

  if ( mine_bad != their_bad || mine_short != their_short ) {
    char d[192];
    snprintf( d, sizeof d, "%s -- mine %lld, libc %lld", hex( bytes, n ).c_str(), (long long)mine_ret,
              (long long)their_ret );
    fail( "disagreement on validity", d );
    return;
  }
  if ( mine_bad || mine_short ) {
    return;
  }
  if ( mine_ret != their_ret || mine != (char32_t)theirs ) {
    char d[192];
    snprintf( d, sizeof d, "%s -- mine U+%04X/%zu, libc U+%04X/%zu", hex( bytes, n ).c_str(), (unsigned)mine,
              mine_ret, (unsigned)theirs, their_ret );
    fail( "disagreement on value", d );
  }
}

/* Does this C library accept the obsolete five- and six-byte forms?
 *
 * glibc does, and this codec deliberately matches it (see UTF8_MAX_CODE_POINT
 * in utf8.h): a client and server that disagree about how many U+FFFDs a
 * malformed sequence produces desynchronise the screen. Apple's libc does not
 * -- it implements strict Unicode, stopping at U+10FFFF -- so requiring
 * agreement above that range is requiring the impossible, and made this test
 * fail on macOS while passing everywhere it had been run.
 *
 * Probe rather than #ifdef __GLIBC__: what matters is the behaviour in front
 * of us, not the vendor's name. */
static bool libc_accepts_extended_forms( void )
{
  /* U+200000 as a five-byte form: F8 88 80 80 80. */
  static const char five[] = "\xF8\x88\x80\x80\x80";
  wchar_t wc = 0;
  mbstate_t ps = mbstate_t();
  return mbrtowc( &wc, five, sizeof five - 1, &ps ) == 5;
}

static void test_against_libc( void )
{
  libc_extended = libc_accepts_extended_forms();
  const bool extended = libc_extended;
  printf( "utf8: this C library %s the obsolete 5- and 6-byte forms\n",
          extended ? "accepts" : "rejects" );
  /* Every one-byte and two-byte sequence: 65792 cases, which between them
     cover bare continuations, both overlong forms' lead bytes, and the
     surrogate and out-of-range lead/second-byte pairs. */
  char buf[2];
  for ( unsigned b0 = 0; b0 < 256; b0++ ) {
    buf[0] = (char)b0;
    compare_with_libc( buf, 1 );
    for ( unsigned b1 = 0; b1 < 256; b1++ ) {
      buf[1] = (char)b1;
      compare_with_libc( buf, 2 );
    }
  }

  /* Every scalar value's canonical encoding. Above U+10FFFF only where the
     library claims to speak the same dialect; elsewhere the comparison would
     be against a library that has decided, correctly by the modern standard,
     that those code points do not exist. Our own behaviour up there is still
     covered -- by test_roundtrip, which does not consult libc at all. */
  const char32_t compare_limit = extended ? Util::UTF8_MAX_CODE_POINT : 0x10FFFF;
  for ( char32_t wc = 0;; wc += ( wc < 0x110000 ? 1 : 0x3FF ) ) {
    if ( wc > compare_limit ) {
      break;
    }
    if ( wc >= 0xD800 && wc <= 0xDFFF ) {
      continue;
    }
    char enc[Util::UTF8_MAX_LEN];
    size_t n = Util::utf8_encode( enc, wc );
    compare_with_libc( enc, n );

    /* And the same bytes with wcrtomb, so the encoder is checked too. */
    if ( wc != 0 ) {
      char theirs[MB_LEN_MAX];
      mbstate_t ps = mbstate_t();
      size_t tn = wcrtomb( theirs, (wchar_t)wc, &ps );
      checks++;
      if ( tn == (size_t)-1 || tn != n || memcmp( theirs, enc, n ) != 0 ) {
        char d[160];
        snprintf( d, sizeof d, "U+%04X: mine %s, libc %s", (unsigned)wc, hex( enc, n ).c_str(),
                  tn == (size_t)-1 ? "(error)" : hex( theirs, tn ).c_str() );
        fail( "encoder disagreement", d );
      }
    }
  }
}

int main( void )
{
  printf( "utf8: self-consistency\n" );
  test_roundtrip();
  test_non_scalars_encode_as_replacement();
  test_known_bad();

  if ( utf8_locale() ) {
    printf( "utf8: differential against the C library (wchar_t is %zu bytes)\n", sizeof( wchar_t ) );
    test_against_libc();
  } else {
    printf( "utf8: skipping the differential test -- no UTF-8 locale with a 32-bit wchar_t\n" );
    printf( "      (expected on Windows; the checks above are the ones that matter there)\n" );
  }

  printf( "utf8: %d checks, %d failures\n", checks, failures );
  return failures ? 1 : 0;
}
