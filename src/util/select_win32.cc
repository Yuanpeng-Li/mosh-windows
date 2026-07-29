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

/* Windows implementation of Select.
 *
 * There is no call here that waits on both a socket and a console. select()
 * takes only SOCKETs; WaitForMultipleObjects takes only kernel objects, and a
 * socket is not one until WSAEventSelect associates an event with it. So:
 * every socket gets an event, consoles are waited on directly, and everything
 * goes into one WaitForMultipleObjects.
 *
 * Two things here are easy to get wrong and are the reason for the extra
 * bookkeeping below.
 *
 * WaitForMultipleObjects reports only the *lowest* signalled index. Believing
 * it means that whichever object sorts first starves the others -- input works
 * but the network stalls, or the reverse, intermittently and only under load.
 * So the return value is used solely to know that *something* happened, and
 * every object is then polled with a zero timeout to find all of them.
 *
 * An anonymous pipe is accepted by WaitForMultipleObjects and is permanently
 * signalled, because it is not a synchronisation object. Waiting on one spins.
 * Pipes are therefore polled with PeekNamedPipe, and their presence shortens
 * the wait so that latency stays inside mosh's frame budget.
 */

#include "src/include/config.h"

#include "src/util/select.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include "src/util/fatal_assert.h"
#include "src/util/timestamp.h"

unsigned int Select::verbose = 0;
std::atomic<int> Select::got_signal[Select::MAX_SIGNAL_NUMBER + 1];

namespace {

/* mosh's frame budget is about 16ms, so a pipe polled this often adds no
   perceptible latency, and the cost is paid only when a pipe is registered --
   that is, in mosh-server, which is otherwise idle. */
const int PIPE_POLL_MS = 4;

struct SocketEntry
{
  Network::socket_t sock;
  WSAEVENT event;
  bool ready;
};

struct HandleEntry
{
  HANDLE handle;
  bool is_pipe;
  bool ready;
};

}

struct Select::Win32Impl
{
  std::vector<SocketEntry> sockets;
  std::vector<HandleEntry> handles;
  /* Set by the console control handler so a wait in progress returns at once
     rather than sitting out the whole timeout. */
  HANDLE wakeup;

  Win32Impl() : wakeup( CreateEventW( NULL, TRUE, FALSE, NULL ) ) {}
  ~Win32Impl()
  {
    for ( size_t i = 0; i < sockets.size(); i++ ) {
      WSACloseEvent( sockets[i].event );
    }
    if ( wakeup ) {
      CloseHandle( wakeup );
    }
  }
};

static Select::Win32Impl* g_impl = NULL;

Select& Select::get_instance( void )
{
  static Select instance;
  return instance;
}

Select::Select() : consecutive_polls( 0 ), impl( new Win32Impl() )
{
  g_impl = impl;
  clear_got_signal();
}

Select::~Select()
{
  g_impl = NULL;
  delete impl;
}

void Select::clear_got_signal( void )
{
  for ( int i = 0; i <= MAX_SIGNAL_NUMBER; i++ ) {
    got_signal[i].store( 0, std::memory_order_relaxed );
  }
}

void Select::add_socket( Network::socket_t s )
{
  for ( size_t i = 0; i < impl->sockets.size(); i++ ) {
    if ( impl->sockets[i].sock == s ) {
      return;
    }
  }
  SocketEntry e;
  e.sock = s;
  e.event = WSACreateEvent();
  e.ready = false;
  if ( e.event == WSA_INVALID_EVENT ) {
    fprintf( stderr, "WSACreateEvent failed: %d\n", WSAGetLastError() );
    return;
  }
  /* FD_CLOSE as well as FD_READ: a socket that has been torn down must wake
     the loop rather than leave it waiting for data that will never come. */
  if ( WSAEventSelect( s, e.event, FD_READ | FD_CLOSE ) != 0 ) {
    fprintf( stderr, "WSAEventSelect failed: %d\n", WSAGetLastError() );
    WSACloseEvent( e.event );
    return;
  }
  impl->sockets.push_back( e );
}

void Select::add_handle( mosh_fd_t h )
{
  HANDLE handle = static_cast<HANDLE>( h );
  if ( handle == NULL || handle == INVALID_HANDLE_VALUE ) {
    /* WaitForMultipleObjects fails outright on one bad handle, which would
       turn the whole wait into a busy loop rather than a reported error. */
    return;
  }
  for ( size_t i = 0; i < impl->handles.size(); i++ ) {
    if ( impl->handles[i].handle == handle ) {
      return;
    }
  }
  HandleEntry e;
  e.handle = handle;
  e.is_pipe = ( GetFileType( handle ) == FILE_TYPE_PIPE );
  e.ready = false;
  impl->handles.push_back( e );
}

void Select::clear_fds( void )
{
  for ( size_t i = 0; i < impl->sockets.size(); i++ ) {
    /* Undo the association, so the socket is left as it was found. */
    WSAEventSelect( impl->sockets[i].sock, NULL, 0 );
    WSACloseEvent( impl->sockets[i].event );
  }
  impl->sockets.clear();
  impl->handles.clear();
}

/* Console control events arrive on a thread Windows creates for the purpose,
   with roughly five seconds before it kills the process if the handler has not
   returned. So: record, wake the waiter, return. Nothing else. */
static BOOL WINAPI console_ctrl_handler( DWORD type )
{
  int signum = 0;
  switch ( type ) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
      signum = SIGINT;
      break;
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      signum = SIGTERM;
      break;
    default:
      return FALSE;
  }

  Select::get_instance().record_signal( signum );
  return TRUE;
}

void Select::record_signal( int signum )
{
  if ( signum < 0 || signum > MAX_SIGNAL_NUMBER ) {
    return;
  }
  got_signal[signum].store( 1, std::memory_order_relaxed );
  if ( g_impl && g_impl->wakeup ) {
    SetEvent( g_impl->wakeup );
  }
}

void Select::add_signal( int signum )
{
  fatal_assert( signum >= 0 );
  fatal_assert( signum <= MAX_SIGNAL_NUMBER );

  /* Only these two have a Windows counterpart.
   *
   * SIGWINCH is not among them on purpose: a console resize arrives as a
   * WINDOW_BUFFER_SIZE_EVENT in the input stream, which the client reads like
   * any other input, so there is nothing to register here.
   *
   * SIGHUP and SIGPIPE have no analogue -- a broken pipe is a failed write,
   * reported where it happens. Registering them is accepted and does nothing,
   * so callers need no #ifdef.
   */
  static bool installed = false;
  if ( ( signum == SIGINT || signum == SIGTERM ) && !installed ) {
    SetConsoleCtrlHandler( console_ctrl_handler, TRUE );
    installed = true;
  }
}

int Select::select( int timeout )
{
  for ( size_t i = 0; i < impl->sockets.size(); i++ ) {
    impl->sockets[i].ready = false;
  }
  for ( size_t i = 0; i < impl->handles.size(); i++ ) {
    impl->handles[i].ready = false;
  }
  /* got_signal is deliberately not cleared here. On POSIX the signals of
     interest are blocked outside pselect(), so nothing can be lost by
     clearing; here they are recorded by other threads -- the console control
     handler and the window-size watcher -- at moments this code does not
     control. They are consumed by signal() instead. */
  ResetEvent( impl->wakeup );

  /* Same poll rate-limiting as the POSIX side. */
  if ( verbose > 1 && timeout == 0 ) {
    fprintf( stderr, "%s: got poll (timeout 0)\n", __func__ );
  }
  if ( timeout == 0 && ++consecutive_polls >= MAX_POLLS ) {
    if ( verbose > 1 && consecutive_polls == MAX_POLLS ) {
      fprintf( stderr, "%s: got %d polls, rate limiting.\n", __func__, MAX_POLLS );
    }
    timeout = 1;
  } else if ( timeout != 0 && consecutive_polls ) {
    consecutive_polls = 0;
  }

  /* Build the wait set. Pipes are excluded: they are always signalled. */
  std::vector<HANDLE> waitset;
  waitset.push_back( impl->wakeup );
  for ( size_t i = 0; i < impl->sockets.size(); i++ ) {
    waitset.push_back( impl->sockets[i].event );
  }
  bool have_pipe = false;
  for ( size_t i = 0; i < impl->handles.size(); i++ ) {
    if ( impl->handles[i].is_pipe ) {
      have_pipe = true;
    } else {
      waitset.push_back( impl->handles[i].handle );
    }
  }

  DWORD wait_ms = ( timeout < 0 ) ? INFINITE : static_cast<DWORD>( timeout );
  if ( have_pipe && ( wait_ms == INFINITE || wait_ms > (DWORD)PIPE_POLL_MS ) ) {
    wait_ms = PIPE_POLL_MS;
  }
  if ( waitset.size() > MAXIMUM_WAIT_OBJECTS ) {
    fprintf( stderr, "select: too many objects to wait on (%zu)\n", waitset.size() );
    waitset.resize( MAXIMUM_WAIT_OBJECTS );
  }

  if ( verbose > 1 ) {
    fprintf( stderr, "%s: waiting on %zu sockets and %zu handles for %ld ms\n", __func__, impl->sockets.size(),
             impl->handles.size(), wait_ms == INFINITE ? -1L : static_cast<long>( wait_ms ) );
  }

  const DWORD waited = WaitForMultipleObjects( static_cast<DWORD>( waitset.size() ), &waitset[0], FALSE, wait_ms );

  /* Which object it names is deliberately ignored below: it reports one index,
     and trusting it starves every object that sorts after the busiest one. So
     everything is polled instead.

     WAIT_FAILED is a different matter and must not be ignored. It returns
     immediately rather than waiting, so the poll below finds nothing, and the
     caller loops straight back in here -- a silent 100% of a core, for as long
     as the condition lasts, in a program whose job is to sit idle for days.
     Report it and fail, so it is a diagnosable error rather than a hot
     laptop. */
  if ( waited == WAIT_FAILED ) {
    const DWORD err = GetLastError();
    fprintf( stderr, "select: WaitForMultipleObjects on %zu objects failed (error %lu)\n", waitset.size(),
             static_cast<unsigned long>( err ) );
    errno = EINVAL;
    return -1;
  }

  int ready = 0;

  for ( size_t i = 0; i < impl->sockets.size(); i++ ) {
    WSANETWORKEVENTS ev;
    memset( &ev, 0, sizeof ev );
    /* Also resets the event, which is what re-arms FD_READ. */
    if ( WSAEnumNetworkEvents( impl->sockets[i].sock, impl->sockets[i].event, &ev ) == 0 ) {
      if ( ev.lNetworkEvents & ( FD_READ | FD_CLOSE ) ) {
        impl->sockets[i].ready = true;
        ready++;
      }
    }
  }

  for ( size_t i = 0; i < impl->handles.size(); i++ ) {
    HandleEntry& h = impl->handles[i];
    if ( h.is_pipe ) {
      DWORD avail = 0;
      if ( PeekNamedPipe( h.handle, NULL, 0, NULL, &avail, NULL ) ) {
        if ( avail > 0 ) {
          h.ready = true;
          ready++;
        }
      } else {
        /* The writer has gone: report it readable so the caller's read sees
           the end of file, exactly as a POSIX pipe does. */
        h.ready = true;
        ready++;
      }
    } else if ( WaitForSingleObject( h.handle, 0 ) == WAIT_OBJECT_0 ) {
      h.ready = true;
      ready++;
    }
  }

  if ( verbose > 1 ) {
    fprintf( stderr, "%s: %d ready\n", __func__, ready );
  }

  freeze_timestamp();

  return ready;
}

bool Select::read( Network::socket_t s ) const
{
  for ( size_t i = 0; i < impl->sockets.size(); i++ ) {
    if ( impl->sockets[i].sock == s ) {
      return impl->sockets[i].ready;
    }
  }
  return false;
}

bool Select::read_handle( mosh_fd_t h ) const
{
  HANDLE handle = static_cast<HANDLE>( h );
  for ( size_t i = 0; i < impl->handles.size(); i++ ) {
    if ( impl->handles[i].handle == handle ) {
      return impl->handles[i].ready;
    }
  }
  return false;
}

bool Select::signal( int signum )
{
  fatal_assert( signum >= 0 );
  fatal_assert( signum <= MAX_SIGNAL_NUMBER );
  return got_signal[signum].exchange( 0, std::memory_order_relaxed ) != 0;
}

bool Select::any_signal( void ) const
{
  for ( int i = 0; i < MAX_SIGNAL_NUMBER; i++ ) {
    if ( got_signal[i].load( std::memory_order_relaxed ) ) {
      return true;
    }
  }
  return false;
}

void Select::set_verbose( unsigned int s_verbose )
{
  verbose = s_verbose;
}
