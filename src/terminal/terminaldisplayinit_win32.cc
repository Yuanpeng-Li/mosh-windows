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

/* Windows has no terminfo, so the capabilities are compiled in.
 *
 * That sounds worse than it is. The terminfo surface here is five values, and
 * one of them -- has_title -- was never read from terminfo even on Unix: the
 * POSIX file matches TERM against a hardcoded list of prefixes, because
 * terminfo has no reliable entry for it.
 *
 * The target is a console with ENABLE_VIRTUAL_TERMINAL_PROCESSING, which the
 * client turns on: conhost on Windows 10 1809 and later, and Windows Terminal.
 * Both are xterm-compatible for everything mosh emits.
 */

#include "src/include/config.h"
#include "terminaldisplay.h"

#include <cstdlib>

using namespace Terminal;

Display::Display( bool use_environment ) : has_ech( true ), has_bce( false ), has_title( true ), smcup( NULL ), rmcup( NULL )
{
  if ( !use_environment ) {
    /* Explicit, rather than leaning on the initialisers above: the POSIX
       constructor's defaults are has_ech(true), has_bce(true), and a future
       reader should not have to work out which of those survive here. */
    has_ech = true;
    has_bce = false;
    has_title = true;
    return;
  }

  /* ECH -- CSI n X. Supported by conhost's VT engine and by Windows Terminal.
     terminaldisplay.cc only tests this for truthiness and then emits the
     sequence literally, so there is nothing to look up. */
  has_ech = true;

  /* BCE -- whether an erase fills with the current background colour.
     Deliberately false to start with. Getting it wrong in this direction
     costs bandwidth, because mosh then repaints cells it could have erased;
     getting it wrong the other way renders incorrectly, leaving the wrong
     background behind. Worth revisiting once it has been checked against
     conhost on the oldest supported build rather than only Windows Terminal. */
  has_bce = false;

  /* OSC 0 sets the window title on both conhost and Windows Terminal. */
  has_title = true;

  if ( !getenv( "MOSH_NO_TERM_INIT" ) ) {
    /* Alternate screen buffer. 1049 rather than 47 or 1047: it saves and
       restores the cursor as well, which is what the Unix smcup/rmcup for a
       modern xterm expands to. */
    smcup = "\033[?1049h";
    rmcup = "\033[?1049l";
  }
}
