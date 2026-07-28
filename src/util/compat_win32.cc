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

/* Small POSIX functions the MSVC CRT does not have. */

#include "src/util/compat.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* --------------------------------------------------------------- getopt --
 *
 * Short options only, which is all mosh uses: "#:cv" and "@:i:p:c:svl:".
 *
 * Written out rather than pulled in because the behaviour mosh depends on is
 * narrow and specific -- in particular mosh-server calls
 * getopt( argc - 1, argv + 1, ... ), so parsing has to start from a clean
 * state each time rather than from wherever a previous call left off. optind
 * is therefore honoured as an input, not merely reported.
 */

char* optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

static int nextchar = 0; /* position within a cluster like -abc */

int getopt( int argc, char* const argv[], const char* optstring )
{
  if ( optind == 0 ) {
    /* The GNU convention for "start over". */
    optind = 1;
    nextchar = 0;
  }

  if ( nextchar == 0 ) {
    if ( optind >= argc || argv[optind] == NULL || argv[optind][0] != '-' || argv[optind][1] == '\0' ) {
      return -1; /* not an option */
    }
    if ( strcmp( argv[optind], "--" ) == 0 ) {
      optind++;
      return -1;
    }
    nextchar = 1;
  }

  const char c = argv[optind][nextchar];
  nextchar++;
  if ( argv[optind][nextchar] == '\0' ) {
    optind++;
    nextchar = 0;
  }

  /* A leading ':' in optstring means "report a missing argument as ':'
     rather than '?'", and is not itself an option. */
  const bool quiet_missing = ( optstring[0] == ':' );
  const char* spec = strchr( optstring + ( quiet_missing ? 1 : 0 ), c );

  if ( spec == NULL || c == ':' ) {
    optopt = c;
    if ( opterr && !quiet_missing ) {
      fprintf( stderr, "%s: invalid option -- '%c'\n", argv[0] ? argv[0] : "mosh", c );
    }
    return '?';
  }

  if ( spec[1] != ':' ) {
    optarg = NULL;
    return c; /* no argument */
  }

  /* Takes an argument: the rest of this word, or the next one. */
  if ( nextchar != 0 ) {
    optarg = const_cast<char*>( argv[optind] + nextchar );
    optind++;
    nextchar = 0;
    return c;
  }
  if ( optind < argc && argv[optind] != NULL ) {
    optarg = const_cast<char*>( argv[optind] );
    optind++;
    return c;
  }

  optopt = c;
  optarg = NULL;
  if ( opterr && !quiet_missing ) {
    fprintf( stderr, "%s: option requires an argument -- '%c'\n", argv[0] ? argv[0] : "mosh", c );
  }
  return quiet_missing ? ':' : '?';
}

/* ------------------------------------------------------------ environment --
 *
 * Windows removes a variable by assigning it an empty value. Note that this
 * does not scrub the old value out of the process environment block, so it is
 * not a way to hide a secret from anything that can read this process's
 * memory -- glibc's unsetenv has the same limitation.
 */

int unsetenv( const char* name )
{
  if ( name == NULL || *name == '\0' || strchr( name, '=' ) != NULL ) {
    return -1;
  }
  return _putenv_s( name, "" ) == 0 ? 0 : -1;
}

int setenv( const char* name, const char* value, int overwrite )
{
  if ( name == NULL || *name == '\0' || strchr( name, '=' ) != NULL ) {
    return -1;
  }
  if ( !overwrite && getenv( name ) != NULL ) {
    return 0;
  }
  return _putenv_s( name, value ? value : "" ) == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ time -- */

int nanosleep( const struct timespec* req, struct timespec* rem )
{
  if ( req == NULL ) {
    return -1;
  }
  /* Sleep's resolution is milliseconds, and it rounds down; mosh's only use
     is a 200ms pause after a decryption failure, where that is immaterial. */
  DWORD ms = static_cast<DWORD>( req->tv_sec ) * 1000 + static_cast<DWORD>( req->tv_nsec / 1000000 );
  Sleep( ms );
  if ( rem ) {
    rem->tv_sec = 0;
    rem->tv_nsec = 0;
  }
  return 0;
}
