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

/* Tests for src/util/uniwidth.
 *
 * Two things are checked, and only one of them can fail the build.
 *
 * Hard requirements -- the contract src/terminal/terminal.cc relies on, and a
 * pinned hash of the whole table so that a regeneration cannot silently move
 * a character's width. A width change is protocol-visible: the client and the
 * server must agree, so it has to be a deliberate, reviewed act.
 *
 * Soft comparison -- the difference against the local C library's wcwidth().
 * This is reported, not enforced. The table is a snapshot of one glibc, and a
 * newer one will legitimately know about code points assigned since. Failing
 * on that would only mean the test breaks whenever the machine is upgraded.
 */

#include "src/include/config.h"

#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if !defined( _WIN32 )
#include <cwchar>
#endif

#include "src/util/uniwidth.h"

/* Pinned so an accidental table regeneration shows up as a test failure
   rather than as characters landing in the wrong column at runtime.
   Regenerate deliberately with scripts/gen-uniwidth.py, then update this. */
static const uint64_t EXPECTED_HASH = 0x1313ab78fa2e8001ULL;

static uint64_t table_hash( void )
{
  /* FNV-1a over every width in order. */
  uint64_t h = 1469598103934665603ULL;
  for ( char32_t cp = 0; cp <= 0x10FFFF; cp++ ) {
    const uint8_t w = static_cast<uint8_t>( Util::uniwidth( cp ) );
    h ^= w;
    h *= 1099511628211ULL;
  }
  return h;
}

int main( void )
{
  int failures = 0;

  /* --- the contract ---------------------------------------------------- */
  for ( char32_t cp = 0; cp <= 0x10FFFF; cp++ ) {
    const int w = Util::uniwidth( cp );
    if ( w < -1 || w > 2 ) {
      fprintf( stderr, "FAIL: U+%04X has width %d, outside {-1,0,1,2}\n", (unsigned)cp, w );
      if ( ++failures > 10 ) {
        return 1;
      }
    }
  }

  /* Above Unicode's range -- the UTF-8 decoder can produce these. */
  const char32_t beyond[] = { 0x110000, 0x140000, 0x1FFFFF, 0x7FFFFFFF };
  for ( size_t i = 0; i < sizeof beyond / sizeof *beyond; i++ ) {
    if ( Util::uniwidth( beyond[i] ) != -1 ) {
      fprintf( stderr, "FAIL: U+%X should be unprintable\n", (unsigned)beyond[i] );
      failures++;
    }
  }

  /* A few anchors, so an entirely wrong table cannot pass. */
  struct
  {
    char32_t cp;
    int w;
    const char* what;
  } anchors[] = {
    { 0x0041, 1, "LATIN CAPITAL LETTER A" },   { 0x0000, 0, "NUL" },
    { 0x0007, -1, "BEL" },                     { 0x001B, -1, "ESC" },
    { 0x00A0, 1, "NBSP" },                     { 0x0300, 0, "COMBINING GRAVE ACCENT" },
    { 0x4E00, 2, "CJK UNIFIED IDEOGRAPH-4E00" }, { 0x3042, 2, "HIRAGANA LETTER A" },
    { 0xFF21, 2, "FULLWIDTH LATIN CAPITAL A" }, { 0xD800, -1, "high surrogate" },
    { 0x1F600, 2, "GRINNING FACE" },           { 0x20000, 2, "CJK ext B" },
    { 0xFFFD, 1, "REPLACEMENT CHARACTER" },    { 0x200B, 0, "ZERO WIDTH SPACE" },
  };
  for ( size_t i = 0; i < sizeof anchors / sizeof *anchors; i++ ) {
    const int got = Util::uniwidth( anchors[i].cp );
    if ( got != anchors[i].w ) {
      fprintf( stderr, "FAIL: U+%04X (%s) width %d, expected %d\n", (unsigned)anchors[i].cp, anchors[i].what, got,
               anchors[i].w );
      failures++;
    }
  }

  const uint64_t h = table_hash();
  printf( "uniwidth: table hash 0x%016llx\n", (unsigned long long)h );
  if ( EXPECTED_HASH != 0 && h != EXPECTED_HASH ) {
    fprintf( stderr, "FAIL: table hash changed (expected 0x%016llx).\n", (unsigned long long)EXPECTED_HASH );
    fprintf( stderr, "      Character widths are protocol-visible: a client and server that\n" );
    fprintf( stderr, "      disagree misplace everything after the first wide character.\n" );
    fprintf( stderr, "      If the regeneration was intended, update EXPECTED_HASH.\n" );
    failures++;
  }

  /* --- comparison with the C library, reported only -------------------- */
#if !defined( _WIN32 )
  if ( setlocale( LC_ALL, "C.UTF-8" ) || setlocale( LC_ALL, "en_US.UTF-8" ) ) {
    if ( sizeof( wchar_t ) >= 4 ) {
      long diff = 0, first = -1;
      for ( char32_t cp = 0; cp <= 0x10FFFF; cp++ ) {
        if ( cp >= 0xD800 && cp <= 0xDFFF ) {
          continue; /* not characters; the table pins these to -1 */
        }
        if ( Util::uniwidth( cp ) != wcwidth( (wchar_t)cp ) ) {
          if ( first < 0 ) {
            first = (long)cp;
          }
          diff++;
        }
      }
      if ( diff == 0 ) {
        printf( "uniwidth: identical to this system's wcwidth()\n" );
      } else {
        printf( "uniwidth: differs from this system's wcwidth() at %ld of 1114112 code points\n", diff );
        printf( "          (first at U+%04lX; expected if the C library knows a newer Unicode\n", first );
        printf( "           than the table was generated from -- reported, not an error)\n" );
      }
    }
  }
#endif

  printf( "uniwidth: %d failures\n", failures );
  return failures ? 1 : 0;
}
