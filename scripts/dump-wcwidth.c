/* Dump the C library's wcwidth() as ranges, for scripts/gen-uniwidth.py.
 *
 * Run on a glibc machine in a UTF-8 locale:
 *     cc -O2 -o dump scripts/dump-wcwidth.c && ./dump > widths.txt
 */

#define _XOPEN_SOURCE 700
#include <locale.h>
#include <stdio.h>
#include <wchar.h>

int main( void )
{
  if ( !setlocale( LC_ALL, "C.UTF-8" ) && !setlocale( LC_ALL, "en_US.UTF-8" ) ) {
    fprintf( stderr, "no UTF-8 locale available\n" );
    return 1;
  }

  int prev = -99;
  unsigned start = 0;
  for ( unsigned cp = 0; cp <= 0x10FFFF; cp++ ) {
    /* Surrogates are not characters; wcwidth's answer for them is not
       meaningful, so pin them to unprintable. */
    int w = ( cp >= 0xD800 && cp <= 0xDFFF ) ? -1 : wcwidth( (wchar_t)cp );
    if ( w < -1 || w > 2 ) {
      fprintf( stderr, "unexpected width %d at U+%04X\n", w, cp );
      return 1;
    }
    if ( w != prev ) {
      if ( prev != -99 ) {
        printf( "%X %X %d\n", start, cp - 1, prev );
      }
      start = cp;
      prev = w;
    }
  }
  printf( "%X %X %d\n", start, 0x10FFFFu, prev );
  return 0;
}
