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

#ifndef SELECT_HPP
#define SELECT_HPP

/* Wait until one of several things is readable, or a signal arrives, or a
 * timeout expires.
 *
 * Sockets and handles are registered separately, and read back separately.
 * On POSIX both are file descriptors and the distinction costs nothing. On
 * Windows they are different kinds of object that cannot be waited on by the
 * same call: select() there takes only SOCKETs, and WaitForMultipleObjects
 * does not accept a socket without WSAEventSelect first turning it into an
 * event. Keeping the two apart in the interface is what avoids inventing a
 * table that maps invented small integers onto real kernel objects -- and
 * then having to keep that table honest.
 */

#include "src/util/compat.h"

#include <csignal>
#include <cstddef>
#include <vector>

#if defined( _WIN32 )
#include <atomic>
#else
#include <sys/select.h>
#endif

namespace Network {
#if defined( _WIN32 )
typedef UINT_PTR socket_t;
#else
typedef int socket_t;
#endif
}

class Select
{
public:
  static Select& get_instance( void );

  /* A socket. On Windows this is a SOCKET; WSAEventSelect makes it waitable. */
  void add_socket( Network::socket_t s );

  /* A handle: the console on the client, the pty on the server. On POSIX this
     is a plain file descriptor and add_socket would do just as well; the two
     names exist so the Windows implementation can tell them apart. */
  void add_handle( mosh_fd_t h );

  void clear_fds( void );

  /* Registers interest in a signal. On Windows the signal numbers are the
     familiar names for events that are not signals at all -- a console control
     event, or a named event another process sets. */
  static void add_signal( int signum );

  /* Waits. timeout is in milliseconds; negative means forever. Returns the
     number of ready objects, or 0 if the wait ended for another reason
     (timeout, or a signal). Never returns an error to the caller: a signal is
     reported through signal(). */
  int select( int timeout );

  bool read( Network::socket_t s ) const;
  bool read_handle( mosh_fd_t h ) const;

  /* Consumes one signal notification. */
  bool signal( int signum );
  /* Does not consume. */
  bool any_signal( void ) const;

  static void set_verbose( unsigned int s_verbose );

  static const int MAX_SIGNAL_NUMBER = 64;

private:
  Select();
  Select( const Select& );
  Select& operator=( const Select& );

  void clear_got_signal( void );

  /* Number of zero-timeout selects after which something is probably wrong. */
  static const int MAX_POLLS = 10;
  int consecutive_polls;
  static unsigned int verbose;

#if defined( _WIN32 )
  /* Written by the console control handler, which Windows runs on a thread of
     its own -- so std::atomic rather than sig_atomic_t, which promises nothing
     across threads. */
  static std::atomic<int> got_signal[MAX_SIGNAL_NUMBER + 1];

public:
  /* Called by the console control handler, which Windows runs on a thread of
     its own -- hence public and atomic. */
  void record_signal( int signum );

  struct Win32Impl;
  ~Select();

private:
  Win32Impl* impl;
#else
  static void handle_signal( int signum );

  int max_fd;
  /* Writes are assumed atomic; concurrent handlers are masked out as well. */
  volatile sig_atomic_t got_signal[MAX_SIGNAL_NUMBER + 1];
  fd_set all_fds, read_fds;
  sigset_t empty_sigset;
  static fd_set dummy_fd_set;
  static sigset_t dummy_sigset;
#endif
};

#endif
