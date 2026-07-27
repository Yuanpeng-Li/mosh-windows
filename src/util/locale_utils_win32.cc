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

/* Windows counterpart of locale_utils.cc.
 *
 * Windows has no LANG or LC_* convention: nl_langinfo does not exist, and the
 * environment variables the POSIX version consults are simply absent. The
 * character set a Windows program sees is the process code page and, for
 * display, the console's -- neither of which is a "locale" in the POSIX sense.
 *
 * A separate file rather than #ifdefs, because the two are not the same
 * function with a different spelling: they answer different questions.
 */

#include "src/include/config.h"

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "src/util/locale_utils.h"

const std::string LocaleVar::str( void ) const
{
  if ( name.empty() ) {
    return std::string( "[no charset variables]" );
  }
  return name + "=" + value;
}

const LocaleVar get_ctype( void )
{
  /* Reported for diagnostics only. These are not normally set on Windows, but
     honour them if somebody has: a user who exported LANG probably meant it. */
  if ( const char* all = getenv( "LC_ALL" ) ) {
    return LocaleVar( "LC_ALL", all );
  } else if ( const char* ctype = getenv( "LC_CTYPE" ) ) {
    return LocaleVar( "LC_CTYPE", ctype );
  } else if ( const char* lang = getenv( "LANG" ) ) {
    return LocaleVar( "LANG", lang );
  }
  return LocaleVar( "", "" );
}

const char* locale_charset( void )
{
  static char name[32];
  const UINT cp = GetACP();
  if ( cp == CP_UTF8 ) {
    return "UTF-8";
  }
  snprintf( name, sizeof name, "CP%u", cp );
  return name;
}

bool is_utf8_locale( void )
{
  /* Always true, and that is not a fudge.
   *
   * On POSIX this guards a real failure: mosh decoded with mbrtowc, so a
   * non-UTF-8 locale would mangle every multibyte character, and refusing to
   * start was better than corrupting the session. mosh no longer asks the C
   * library to decode anything -- src/util/utf8 does it, independently of the
   * locale -- so that failure mode does not exist here.
   *
   * What remains is display, which is the console's code page rather than a
   * locale, and is set and restored by the console layer rather than probed
   * here. Refusing to start because LANG is unset would fail every Windows
   * machine, since none of them set it.
   */
  return true;
}

void set_native_locale( void )
{
  /* Ask the CRT for UTF-8 so anything still going through it -- printf of a
     multibyte string, mostly diagnostics -- agrees with the rest of mosh.
     Supported by the UCRT since Windows 10 1803; older systems just keep the
     ANSI code page, which only affects those diagnostics.

     Deliberately does not touch SetConsoleOutputCP or SetConsoleCP: those are
     console state, they outlive the process, and leaving a console in the
     wrong code page wedges the user's shell after mosh exits. The console
     layer owns them, because it is what restores them. */
  setlocale( LC_ALL, ".UTF-8" );
}

void clear_locale_variables( void )
{
  /* Windows has no unsetenv; assigning an empty value is how a variable is
     removed from the environment block. */
  static const char* const names[] = { "LANG",        "LANGUAGE",    "LC_CTYPE",    "LC_NUMERIC",
                                       "LC_TIME",     "LC_COLLATE",  "LC_MONETARY", "LC_MESSAGES",
                                       "LC_PAPER",    "LC_NAME",     "LC_ADDRESS",  "LC_TELEPHONE",
                                       "LC_MEASUREMENT", "LC_IDENTIFICATION", "LC_ALL" };
  for ( size_t i = 0; i < sizeof names / sizeof *names; i++ ) {
    _putenv_s( names[i], "" );
  }
}
