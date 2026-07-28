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

#include "src/include/config.h"

#include "src/util/console.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace Terminal {

struct Console::Impl
{
  struct termios saved;
  bool have_saved;

  Impl() : saved(), have_saved( false ) {}
};

Console::Console() : impl( new Impl() ) {}

Console::~Console()
{
  restore();
  delete impl;
}

bool Console::save( void )
{
  if ( tcgetattr( STDIN_FILENO, &impl->saved ) < 0 ) {
    return false;
  }
  impl->have_saved = true;
  return true;
}

bool Console::set_raw( void )
{
  if ( !impl->have_saved && !save() ) {
    perror( "tcgetattr" );
    return false;
  }

  struct termios raw = impl->saved;

#ifdef HAVE_IUTF8
  /* Probably unnecessary since raw mode follows, but preserved from the code
     this replaced. */
  if ( !( raw.c_iflag & IUTF8 ) ) {
    raw.c_iflag |= IUTF8;
  }
#endif

  cfmakeraw( &raw );

  if ( tcsetattr( STDIN_FILENO, TCSANOW, &raw ) < 0 ) {
    perror( "tcsetattr" );
    return false;
  }
  return true;
}

void Console::restore( void )
{
  if ( !impl->have_saved ) {
    return;
  }
  if ( tcsetattr( STDIN_FILENO, TCSANOW, &impl->saved ) < 0 ) {
    perror( "tcsetattr" );
  }
  impl->have_saved = false;
}

bool Console::get_size( int& width, int& height ) const
{
  struct winsize ws;
  if ( ioctl( STDIN_FILENO, TIOCGWINSZ, &ws ) < 0 ) {
    return false;
  }
  width = ws.ws_col;
  height = ws.ws_row;
  return width > 0 && height > 0;
}

bool Console::suspend_self( void )
{
  kill( 0, SIGSTOP );
  return true;
}

mosh_fd_t Console::output( void )
{
  return STDOUT_FILENO;
}

mosh_fd_t Console::input( void )
{
  return STDIN_FILENO;
}


ssize_t Console::read_input( char* buf, size_t len )
{
  const ssize_t n = ::read( STDIN_FILENO, buf, len );
  if ( n < 0 ) {
    perror( "read" );
  }
  return n;
}

}
