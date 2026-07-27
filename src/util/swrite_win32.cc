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

#include <cstdio>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "src/util/swrite.h"

static void report( const char* what )
{
  const DWORD err = GetLastError();
  char buf[256] = { 0 };
  FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, buf, sizeof buf, NULL );
  size_t n = strlen( buf );
  while ( n > 0 && ( buf[n - 1] == '\n' || buf[n - 1] == '\r' ) ) {
    buf[--n] = '\0';
  }
  fprintf( stderr, "%s: %s (%lu)\n", what, buf, (unsigned long)err );
}

int swrite( mosh_fd_t fd, const char* str, ssize_t len )
{
  const size_t total = ( len >= 0 ) ? static_cast<size_t>( len ) : strlen( str );
  size_t written = 0;

  while ( written < total ) {
    /* WriteFile takes a DWORD; a terminal update can in principle exceed 4GB
       in no sane universe, but clamp rather than truncate silently. */
    const size_t chunk = ( total - written > 0x40000000u ) ? 0x40000000u : ( total - written );
    DWORD n = 0;
    if ( !WriteFile( fd, str + written, static_cast<DWORD>( chunk ), &n, NULL ) ) {
      report( "write" );
      return -1;
    }
    if ( n == 0 ) {
      /* A pipe whose reader has gone, or a console that has been closed.
         Treated as failure, as a zero-byte write(2) is on POSIX. */
      fprintf( stderr, "write: wrote nothing\n" );
      return -1;
    }
    written += n;
  }

  return 0;
}

mosh_fd_t mosh_stdout_fd( void )
{
  return GetStdHandle( STD_OUTPUT_HANDLE );
}
