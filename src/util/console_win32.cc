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

#include "src/util/console.h"

#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

namespace Terminal {

struct Console::Impl
{
  HANDLE in;
  HANDLE out;
  DWORD saved_in_mode;
  DWORD saved_out_mode;
  UINT saved_in_cp;
  UINT saved_out_cp;
  bool saved;

  Impl()
    : in( GetStdHandle( STD_INPUT_HANDLE ) ), out( GetStdHandle( STD_OUTPUT_HANDLE ) ), saved_in_mode( 0 ),
      saved_out_mode( 0 ), saved_in_cp( 0 ), saved_out_cp( 0 ), saved( false )
  {}
};

Console::Console() : impl( new Impl() ) {}

Console::~Console()
{
  restore();
  delete impl;
}

bool Console::save( void )
{
  if ( !GetConsoleMode( impl->in, &impl->saved_in_mode ) || !GetConsoleMode( impl->out, &impl->saved_out_mode ) ) {
    return false;
  }
  impl->saved_in_cp = GetConsoleCP();
  impl->saved_out_cp = GetConsoleOutputCP();
  impl->saved = true;
  return true;
}

bool Console::set_raw( void )
{
  if ( !impl->saved && !save() ) {
    return false;
  }

  /* mosh's output is a VT stream, so the console has to interpret it rather
     than treat it as text. DISABLE_NEWLINE_AUTO_RETURN stops the console
     adding a carriage return of its own at the right margin, which would
     break the deferred-wrap assumption the diff algorithm relies on. */
  DWORD out_mode = impl->saved_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
  if ( !SetConsoleMode( impl->out, out_mode ) ) {
    fprintf( stderr,
             "mosh-client: this console cannot interpret terminal sequences "
             "(error %lu).\nWindows 10 1809 or newer is required.\n",
             GetLastError() );
    return false;
  }

  /* Input: one key at a time, no echo, and keys delivered as VT sequences.
     ENABLE_PROCESSED_INPUT off is what makes Ctrl-C arrive as byte 0x03 to be
     forwarded to the remote host, rather than as a console control event.
     ENABLE_WINDOW_INPUT is how resizes arrive -- there is no SIGWINCH.
     QUICK_EDIT off stops a stray click from freezing output; it needs
     EXTENDED_FLAGS to take effect. */
  DWORD in_mode = impl->saved_in_mode;
  in_mode &= ~( ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE );
  in_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
  if ( !SetConsoleMode( impl->in, in_mode ) ) {
    SetConsoleMode( impl->out, impl->saved_out_mode );
    fprintf( stderr, "mosh-client: cannot put the console into raw mode (error %lu).\n", GetLastError() );
    return false;
  }

  /* UTF-8 both ways. Restored with the modes, for the same reason. */
  SetConsoleCP( CP_UTF8 );
  SetConsoleOutputCP( CP_UTF8 );

  return true;
}

void Console::restore( void )
{
  if ( !impl->saved ) {
    return;
  }
  SetConsoleMode( impl->in, impl->saved_in_mode );
  SetConsoleMode( impl->out, impl->saved_out_mode );
  SetConsoleCP( impl->saved_in_cp );
  SetConsoleOutputCP( impl->saved_out_cp );
  impl->saved = false;
}

bool Console::get_size( int& width, int& height ) const
{
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if ( !GetConsoleScreenBufferInfo( impl->out, &csbi ) ) {
    return false;
  }
  /* srWindow, not dwSize. The buffer is typically 9001 rows tall for
     scrollback; the terminal is what the user can see. Note that a
     WINDOW_BUFFER_SIZE_EVENT reports the *buffer* size, so the size must be
     re-read here rather than taken from the event. */
  width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  return width > 0 && height > 0;
}

bool Console::suspend_self( void )
{
  /* No job control on Windows, and nothing a console program can do to
     suspend itself the way Ctrl-Z does. The caller reports this to the user
     rather than silently doing nothing. */
  return false;
}

mosh_fd_t Console::output( void )
{
  return GetStdHandle( STD_OUTPUT_HANDLE );
}

mosh_fd_t Console::input( void )
{
  return GetStdHandle( STD_INPUT_HANDLE );
}


ssize_t Console::read_input( char* buf, size_t len )
{
  /* ReadFile rather than ReadConsoleInput: with ENABLE_VIRTUAL_TERMINAL_INPUT
     the console translates key events into the same escape sequences a POSIX
     terminal driver produces, so the parser above sees one stream on both
     platforms. Window resizes do not appear here -- they arrive as console
     input records and are read separately. */
  DWORD n = 0;
  if ( !ReadFile( GetStdHandle( STD_INPUT_HANDLE ), buf, static_cast<DWORD>( len ), &n, NULL ) ) {
    const DWORD err = GetLastError();
    if ( err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF ) {
      return 0;
    }
    fprintf( stderr, "read: console read failed (error %lu)\n", err );
    return -1;
  }
  return static_cast<ssize_t>( n );
}

}
