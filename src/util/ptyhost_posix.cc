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

#include "src/util/ptyhost.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <err.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "src/util/select.h"
#include "src/util/swrite.h"

struct PtyHost::Impl
{
  int master;
  int release_pipe;

  Impl() : master( -1 ), release_pipe( -1 ) {}
};

PtyHost::PtyHost() : impl( new Impl() ) {}

PtyHost::~PtyHost()
{
  close();
  delete impl;
}

void PtyHost::adopt( int master_fd, int release_pipe_fd )
{
  impl->master = master_fd;
  impl->release_pipe = release_pipe_fd;
}

int PtyHost::master( void ) const
{
  return impl->master;
}

void PtyHost::add_to( Select& sel ) const
{
  if ( impl->master >= 0 ) {
    sel.add_handle( impl->master );
  }
}

bool PtyHost::readable( const Select& sel ) const
{
  return impl->master >= 0 && sel.read_handle( impl->master );
}

bool PtyHost::exited( const Select& ) const
{
  /* Arrives as end of file on the master instead. */
  return false;
}

ssize_t PtyHost::read( char* buf, size_t len )
{
  return ::read( impl->master, buf, len );
}

int PtyHost::write( const char* buf, size_t len )
{
  return swrite( impl->master, buf, static_cast<ssize_t>( len ) );
}

bool PtyHost::resize( int width, int height )
{
  struct winsize window_size;
  if ( ioctl( impl->master, TIOCGWINSZ, &window_size ) < 0 ) {
    perror( "ioctl TIOCGWINSZ" );
    return false;
  }
  window_size.ws_col = width;
  window_size.ws_row = height;
  if ( ioctl( impl->master, TIOCSWINSZ, &window_size ) < 0 ) {
    perror( "ioctl TIOCSWINSZ" );
    return false;
  }
  return true;
}

void PtyHost::release( void )
{
  if ( impl->release_pipe < 0 ) {
    return;
  }
  const int fd = impl->release_pipe;
  impl->release_pipe = -1;
  if ( ::close( fd ) < 0 ) {
    err( 1, "child release" );
  }
}

void PtyHost::close( void )
{
  if ( impl->release_pipe >= 0 ) {
    ::close( impl->release_pipe );
    impl->release_pipe = -1;
  }
  if ( impl->master >= 0 ) {
    const int fd = impl->master;
    impl->master = -1;
    if ( ::close( fd ) < 0 ) {
      perror( "close" );
      exit( 1 );
    }
  }
}
