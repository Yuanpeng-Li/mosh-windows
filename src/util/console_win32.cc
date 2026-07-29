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

/* Two things about a Windows console have no POSIX counterpart and shape
 * everything below.
 *
 * Its input handle cannot be used as a readiness signal. It is signalled when
 * an input *record* is queued, but reading it returns only the records the
 * console translates into bytes -- so a focus change, a mouse move or a resize
 * makes it ready, and the read that follows blocks. There is no non-blocking
 * read of a console and no way to ask "would a read return", short of peeking
 * the records and reimplementing the console's own key-to-VT translation well
 * enough to predict a bare Shift press. So the read is moved to a thread,
 * where blocking costs nothing, and the main loop waits on an event that is
 * set only when bytes are genuinely in hand.
 *
 * A resize is not a signal. It arrives as a WINDOW_BUFFER_SIZE_EVENT record,
 * which the reader's ReadFile discards on its way to finding characters. The
 * window size is therefore sampled by a second thread, which is both simpler
 * than intercepting the record and correct in the cases the record misses --
 * a font change resizes the terminal without one.
 */

#include "src/util/console.h"

#include "src/util/select.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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

namespace {
/* Fast enough that a drag-resize looks continuous, slow enough that the cost
   is one cheap call ten times a second. */
const DWORD RESIZE_POLL_MS = 100;
}

/* At namespace scope so the thread entry points below can name it:
   Console::Impl is private and they are not members. */
struct ConsoleState
{
  HANDLE in;
  HANDLE out;
  DWORD saved_in_mode;
  DWORD saved_out_mode;
  UINT saved_in_cp;
  UINT saved_out_cp;
  bool saved;

  /* Shared with the reader thread. */
  CRITICAL_SECTION lock;
  std::string pending;
  bool eof;
  bool is_console; /* false if stdin was redirected, where a zero-byte read
                      really does mean end of input */

  HANDLE data_event;  /* manual reset: set while pending is non-empty or eof */
  HANDLE stop_event;  /* asks the size watcher to exit */
  HANDLE reader;
  HANDLE watcher;

  ConsoleState()
    : in( GetStdHandle( STD_INPUT_HANDLE ) ), out( GetStdHandle( STD_OUTPUT_HANDLE ) ), saved_in_mode( 0 ),
      saved_out_mode( 0 ), saved_in_cp( 0 ), saved_out_cp( 0 ), saved( false ), pending(), eof( false ),
      is_console( false ), data_event( CreateEventW( NULL, TRUE, FALSE, NULL ) ),
      stop_event( CreateEventW( NULL, TRUE, FALSE, NULL ) ), reader( NULL ), watcher( NULL )
  {
    InitializeCriticalSection( &lock );
  }

  ~ConsoleState()
  {
    if ( data_event ) {
      CloseHandle( data_event );
    }
    if ( stop_event ) {
      CloseHandle( stop_event );
    }
    DeleteCriticalSection( &lock );
  }

private:
  ConsoleState( const ConsoleState& );
  ConsoleState& operator=( const ConsoleState& );
};

struct Console::Impl : public ConsoleState
{};

/* input() and read_input() are static -- they are what Select is handed, and
   there is one console per process. */
static ConsoleState* g_impl = NULL;

static DWORD WINAPI reader_main( LPVOID param )
{
  ConsoleState* impl = static_cast<ConsoleState*>( param );
  char buf[4096];

  for ( ;; ) {
    DWORD n = 0;
    const BOOL ok = ReadFile( impl->in, buf, sizeof buf, &n, NULL );
    if ( !ok ) {
      break; /* handle closed, or the read was cancelled on the way out */
    }
    if ( n == 0 ) {
      /* On a real console this is not end of input: it is a keystroke the
         console had nothing to translate into, such as a bare modifier.
         Treating it as EOF would exit the client when the user pressed
         Shift. Only a redirected stdin means it for real. */
      if ( impl->is_console ) {
        continue;
      }
      break;
    }

    EnterCriticalSection( &impl->lock );
    impl->pending.append( buf, n );
    SetEvent( impl->data_event );
    LeaveCriticalSection( &impl->lock );
  }

  EnterCriticalSection( &impl->lock );
  impl->eof = true;
  SetEvent( impl->data_event );
  LeaveCriticalSection( &impl->lock );
  return 0;
}

static DWORD WINAPI watcher_main( LPVOID param )
{
  ConsoleState* impl = static_cast<ConsoleState*>( param );

  int width = 0, height = 0;
  {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if ( GetConsoleScreenBufferInfo( impl->out, &csbi ) ) {
      width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
      height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
  }

  while ( WaitForSingleObject( impl->stop_event, RESIZE_POLL_MS ) == WAIT_TIMEOUT ) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if ( !GetConsoleScreenBufferInfo( impl->out, &csbi ) ) {
      continue;
    }
    const int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    const int h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    if ( w == width && h == height ) {
      continue;
    }
    width = w;
    height = h;
    /* Delivered the same way a SIGWINCH would be, so the client's loop needs
       no Windows-specific branch. record_signal() also wakes a wait in
       progress. */
    Select::get_instance().record_signal( SIGWINCH );
  }
  return 0;
}

Console::Console() : impl( new Impl() )
{
  g_impl = impl;
}

Console::~Console()
{
  restore();

  if ( impl->stop_event ) {
    SetEvent( impl->stop_event );
  }
  /* Both threads have to be accounted for, not just the reader. The watcher
     samples the console every RESIZE_POLL_MS and reads impl the whole time; if
     it has not come out, freeing impl is a use-after-free just as surely.
     The result was previously discarded here, so the leak-rather-than-free
     decision below was made on half the evidence. */
  bool watcher_stopped = true;
  if ( impl->watcher ) {
    watcher_stopped = ( WaitForSingleObject( impl->watcher, 1000 ) == WAIT_OBJECT_0 );
    CloseHandle( impl->watcher );
    impl->watcher = NULL;
  }

  /* The reader is parked in ReadFile and there is no other way to get it out.
     If it will not come out -- CancelSynchronousIo is racy against a read
     that has not started yet -- the state it points at is deliberately leaked
     rather than freed underneath it. This runs as the process exits. */
  bool reader_stopped = true;
  if ( impl->reader ) {
    CancelSynchronousIo( impl->reader );
    reader_stopped = ( WaitForSingleObject( impl->reader, 1000 ) == WAIT_OBJECT_0 );
    CloseHandle( impl->reader );
    impl->reader = NULL;
  }

  g_impl = NULL;
  if ( reader_stopped && watcher_stopped ) {
    delete impl;
  }
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

  /* Only now, with VT input in effect, is it safe to start reading: the
     reader blocks in the console, so any mode change after this point would
     not take effect until the next keystroke. */
  if ( !impl->reader ) {
    if ( impl->data_event == NULL || impl->stop_event == NULL ) {
      fprintf( stderr, "mosh-client: cannot create console events (error %lu).\n", GetLastError() );
      return false;
    }
    DWORD unused_mode = 0;
    impl->is_console = ( GetConsoleMode( impl->in, &unused_mode ) != 0 );
    impl->reader = CreateThread( NULL, 0, reader_main, impl, 0, NULL );
    if ( impl->reader == NULL ) {
      fprintf( stderr, "mosh-client: cannot start the console reader (error %lu).\n", GetLastError() );
      return false;
    }
    impl->watcher = CreateThread( NULL, 0, watcher_main, impl, 0, NULL );
    /* A missing watcher costs resize notifications, not the session. */
  }

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
  /* The reader thread's event, not the console -- see the header. Before
     set_raw() there is no reader; MOSH_BAD_FD is never ready, which is the
     truth. */
  return g_impl && g_impl->reader ? static_cast<mosh_fd_t>( g_impl->data_event ) : MOSH_BAD_FD;
}


ssize_t Console::read_input( char* buf, size_t len )
{
  if ( g_impl == NULL || g_impl->reader == NULL ) {
    return 0;
  }

  /* The bytes are whatever the console translated the keys into. With
     ENABLE_VIRTUAL_TERMINAL_INPUT that is the same escape sequences a POSIX
     terminal driver produces, so everything above this sees one stream on
     both platforms. */
  ssize_t ret;
  EnterCriticalSection( &g_impl->lock );
  const size_t n = g_impl->pending.size() < len ? g_impl->pending.size() : len;
  if ( n > 0 ) {
    memcpy( buf, g_impl->pending.data(), n );
    g_impl->pending.erase( 0, n );
    ret = static_cast<ssize_t>( n );
  } else {
    /* Nothing waiting means the reader is gone: report end of input rather
       than a short read the caller would spin on. */
    ret = 0;
  }
  /* Reset inside the lock, so a byte arriving right now cannot be swallowed
     by a reset that is about to happen anyway. */
  if ( g_impl->pending.empty() && !g_impl->eof ) {
    ResetEvent( g_impl->data_event );
  }
  LeaveCriticalSection( &g_impl->lock );

  return ret;
}

}
