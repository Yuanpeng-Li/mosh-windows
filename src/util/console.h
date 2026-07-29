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

#ifndef CONSOLE_HPP
#define CONSOLE_HPP

#include "src/util/compat.h"

#include <string>

/* The local terminal the client draws on.
 *
 * On POSIX this is termios and TIOCGWINSZ. On Windows it is the console modes
 * and GetConsoleScreenBufferInfo, which are not the same shape: there are two
 * handles rather than one, the "raw" flags are a different set, and -- the
 * part that matters -- console state outlives the process. A POSIX program
 * that dies without restoring termios inconveniences the shell it was started
 * from; a Windows program that dies without restoring the console modes leaves
 * the console itself wedged, with echo off and VT input on, for whatever runs
 * next.
 *
 * Hence save() is separate from set_raw(), restore() is idempotent, and the
 * destructor calls it.
 */

namespace Terminal {

class Console
{
public:
  Console();
  ~Console();

  /* Captures the current state, so restore() has something to go back to.
     Returns false if there is no terminal (output redirected to a file). */
  bool save( void );

  /* Character-at-a-time, no echo, and -- on Windows -- VT sequences
     interpreted on the way out and generated on the way in. Ctrl-C is
     delivered as a keystroke rather than as a signal, because mosh forwards it
     to the remote host. */
  bool set_raw( void );

  /* Back to whatever save() captured. Safe to call more than once, and safe to
     call when save() failed. */
  void restore( void );

  bool get_size( int& width, int& height ) const;

  /* Ctrl-Z. Returns false where the platform has no such concept, which is
     Windows -- there is no job control, and the terminal's own Ctrl-Z is not
     something a console program can invoke on itself. */
  static bool suspend_self( void );

  /* Where to write. STDOUT_FILENO on POSIX; the console output handle on
     Windows. */
  static mosh_fd_t output( void );

  /* What to register with Select in order to be woken by keyboard input.
     Ready means read_input() will not block.

     On Windows this is deliberately not the console input handle. That handle
     is signalled whenever any input record is queued -- a focus change, a
     mouse move, a window resize -- while a read of it returns only records the
     console translates into bytes. Waiting on it therefore reports ready and
     then blocks, which is a hang, and the one place it is guaranteed to happen
     is the first iteration after startup, on the focus event. What is returned
     here is an event owned by set_raw()'s reader thread, set only when bytes
     are actually waiting. */
  static mosh_fd_t input( void );

  /* Reads what the user has typed. Returns the byte count, 0 at end of input,
     or -1 on error having reported it. On Windows the console is read as VT
     input -- the mode set by set_raw() -- so this yields the same escape
     sequences a POSIX terminal driver would deliver. */
  static ssize_t read_input( char* buf, size_t len );

private:
  Console( const Console& );
  Console& operator=( const Console& );

  struct Impl;
  Impl* impl;
};

}

#endif
