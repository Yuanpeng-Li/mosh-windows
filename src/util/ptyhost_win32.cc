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

/* Windows pty: a pseudoconsole, and the shell running on it.
 *
 * Three things about CreateProcessW with PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
 * are not obvious and were each measured rather than assumed. They are
 * recorded in docs/windows-port-notes.md; in short:
 *
 *  - The child attaches to the pseudoconsole but still inherits the parent's
 *    standard handles, so under sshd its output goes to sshd's pipe and the
 *    pty carries nothing. STARTF_USESTDHANDLES with all three handles NULL is
 *    what makes console initialisation substitute the pty's own handles.
 *
 *  - CREATE_NO_WINDOW, DETACHED_PROCESS and CREATE_NEW_CONSOLE each break
 *    that: the child gets a different console and the pty receives nothing.
 *    Someone will eventually add CREATE_NO_WINDOW to "hide the window"; the
 *    fatal_assert below is there to make that fail loudly.
 *
 *  - ConPTY does not close the output pipe when the child exits, so the
 *    child's process handle has to be waited on as well or the session hangs
 *    after the shell is gone.
 */

#include "src/util/ptyhost.h"

#include <cstdio>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "src/util/fatal_assert.h"
#include "src/util/select.h"

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

namespace {

std::string win_error( DWORD err )
{
  char* text = NULL;
  const DWORD n = FormatMessageA( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                    | FORMAT_MESSAGE_IGNORE_INSERTS,
                                  NULL,
                                  err,
                                  0,
                                  reinterpret_cast<char*>( &text ),
                                  0,
                                  NULL );
  std::string out;
  if ( n && text ) {
    out.assign( text, n );
    while ( !out.empty() && ( out[out.size() - 1] == '\n' || out[out.size() - 1] == '\r' ) ) {
      out.erase( out.size() - 1 );
    }
  } else {
    char buf[32];
    snprintf( buf, sizeof buf, "error %lu", static_cast<unsigned long>( err ) );
    out = buf;
  }
  if ( text ) {
    LocalFree( text );
  }
  return out;
}

/* CreateProcessW takes one command line, not a vector, and the rules for
   getting back to a vector are the ones CommandLineToArgvW implements: a run
   of backslashes is doubled only when it precedes a quote. */
std::wstring quote_arg( const std::string& arg )
{
  std::wstring wide;
  const int need = MultiByteToWideChar( CP_UTF8, 0, arg.data(), static_cast<int>( arg.size() ), NULL, 0 );
  if ( need > 0 ) {
    wide.resize( need );
    MultiByteToWideChar( CP_UTF8, 0, arg.data(), static_cast<int>( arg.size() ), &wide[0], need );
  }

  if ( !wide.empty() && wide.find_first_of( L" \t\n\v\"" ) == std::wstring::npos ) {
    return wide;
  }

  std::wstring out( 1, L'"' );
  for ( size_t i = 0;; i++ ) {
    size_t backslashes = 0;
    while ( i < wide.size() && wide[i] == L'\\' ) {
      i++;
      backslashes++;
    }
    if ( i == wide.size() ) {
      out.append( backslashes * 2, L'\\' );
      break;
    }
    if ( wide[i] == L'"' ) {
      out.append( backslashes * 2 + 1, L'\\' );
    } else {
      out.append( backslashes, L'\\' );
    }
    out.push_back( wide[i] );
  }
  out.push_back( L'"' );
  return out;
}

}

struct PtyHost::Impl
{
  HPCON pty;
  HANDLE to_child;   /* we write here; the pty reads */
  HANDLE from_child; /* the pty writes here; we read */
  HANDLE process;
  bool child_gone;

  Impl() : pty( NULL ), to_child( INVALID_HANDLE_VALUE ), from_child( INVALID_HANDLE_VALUE ), process( NULL ),
           child_gone( false )
  {}
};

PtyHost::PtyHost() : impl( new Impl() ) {}

PtyHost::~PtyHost()
{
  close();
  delete impl;
}

bool PtyHost::start( const std::vector<std::string>& args, int width, int height )
{
  if ( args.empty() ) {
    fprintf( stderr, "mosh-server: no command to run\n" );
    return false;
  }

  /* Two pipes, crosswise: the pty's input is what we write, its output is
     what we read. */
  HANDLE pty_in = INVALID_HANDLE_VALUE, pty_out = INVALID_HANDLE_VALUE;
  if ( !CreatePipe( &pty_in, &impl->to_child, NULL, 0 ) ) {
    fprintf( stderr, "mosh-server: CreatePipe: %s\n", win_error( GetLastError() ).c_str() );
    return false;
  }
  if ( !CreatePipe( &impl->from_child, &pty_out, NULL, 0 ) ) {
    fprintf( stderr, "mosh-server: CreatePipe: %s\n", win_error( GetLastError() ).c_str() );
    CloseHandle( pty_in );
    return false;
  }

  COORD size;
  size.X = static_cast<SHORT>( width );
  size.Y = static_cast<SHORT>( height );
  HRESULT hr = CreatePseudoConsole( size, pty_in, pty_out, 0, &impl->pty );
  /* The pseudoconsole took its own references; ours are done either way. */
  CloseHandle( pty_in );
  CloseHandle( pty_out );
  if ( FAILED( hr ) ) {
    fprintf( stderr,
             "mosh-server: CreatePseudoConsole failed (0x%08lx).\n"
             "Windows 10 1809 or newer is required.\n",
             static_cast<unsigned long>( hr ) );
    return false;
  }

  std::wstring cmdline;
  for ( size_t i = 0; i < args.size(); i++ ) {
    if ( i ) {
      cmdline.push_back( L' ' );
    }
    cmdline += quote_arg( args[i] );
  }

  SIZE_T attr_size = 0;
  InitializeProcThreadAttributeList( NULL, 1, 0, &attr_size );
  std::vector<char> attr_buf( attr_size );
  LPPROC_THREAD_ATTRIBUTE_LIST attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>( &attr_buf[0] );
  if ( !InitializeProcThreadAttributeList( attrs, 1, 0, &attr_size )
       || !UpdateProcThreadAttribute(
         attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, impl->pty, sizeof( HPCON ), NULL, NULL ) ) {
    fprintf( stderr, "mosh-server: UpdateProcThreadAttribute: %s\n", win_error( GetLastError() ).c_str() );
    return false;
  }

  STARTUPINFOEXW si;
  memset( &si, 0, sizeof si );
  si.StartupInfo.cb = sizeof si;
  si.lpAttributeList = attrs;
  /* NULL handles, not INVALID_HANDLE_VALUE and not real ones: this is the
     documented "no handles supplied" case, and it is what makes the child use
     the pseudoconsole it is attached to. See the note at the top. */
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = NULL;
  si.StartupInfo.hStdOutput = NULL;
  si.StartupInfo.hStdError = NULL;

  PROCESS_INFORMATION pi;
  memset( &pi, 0, sizeof pi );

  std::vector<wchar_t> mutable_cmdline( cmdline.begin(), cmdline.end() );
  mutable_cmdline.push_back( L'\0' );

  /* The guard the header comment says is here. It was not: the comment above
     promises that adding CREATE_NO_WINDOW to "hide the window" will fail
     loudly, and nothing enforced it, so the failure would instead have been a
     session that starts and shows nothing -- the child attached to a different
     console, the pseudoconsole receiving no output at all.

     Named separately so the assertion has something to test, and so the next
     person to add a flag has to walk past this. */
  const DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
  fatal_assert( 0 == ( creation_flags & ( CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_CONSOLE ) ) );

  const BOOL ok = CreateProcessW( NULL,
                                  &mutable_cmdline[0],
                                  NULL,
                                  NULL,
                                  FALSE,
                                  creation_flags,
                                  NULL,
                                  NULL,
                                  &si.StartupInfo,
                                  &pi );
  const DWORD spawn_error = GetLastError();
  DeleteProcThreadAttributeList( attrs );

  if ( !ok ) {
    fprintf( stderr, "mosh-server: cannot run %s: %s\n", args[0].c_str(), win_error( spawn_error ).c_str() );
    return false;
  }

  CloseHandle( pi.hThread );
  impl->process = pi.hProcess;
  return true;
}

void PtyHost::add_to( Select& sel ) const
{
  if ( impl->from_child != INVALID_HANDLE_VALUE ) {
    sel.add_handle( impl->from_child );
  }
  /* ConPTY keeps the output pipe open after the child exits, so waiting on
     the pipe alone would never notice. */
  if ( impl->process ) {
    sel.add_handle( impl->process );
  }
}

bool PtyHost::readable( const Select& sel ) const
{
  return impl->from_child != INVALID_HANDLE_VALUE && sel.read_handle( impl->from_child );
}

bool PtyHost::exited( const Select& sel ) const
{
  if ( impl->child_gone ) {
    return true;
  }
  return impl->process != NULL && sel.read_handle( impl->process );
}

ssize_t PtyHost::read( char* buf, size_t len )
{
  if ( impl->from_child == INVALID_HANDLE_VALUE ) {
    return 0;
  }

  /* ReadFile on a pipe blocks until at least one byte arrives, and the serve
     loop cannot afford that, so never ask for more than is already there. */
  DWORD avail = 0;
  if ( !PeekNamedPipe( impl->from_child, NULL, 0, NULL, &avail, NULL ) ) {
    return 0; /* writer gone: end of input */
  }
  if ( avail == 0 ) {
    return 0;
  }
  if ( avail < len ) {
    len = avail;
  }

  DWORD n = 0;
  if ( !ReadFile( impl->from_child, buf, static_cast<DWORD>( len ), &n, NULL ) ) {
    const DWORD err = GetLastError();
    if ( err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF ) {
      return 0;
    }
    fprintf( stderr, "mosh-server: read from pty: %s\n", win_error( err ).c_str() );
    return -1;
  }
  return static_cast<ssize_t>( n );
}

int PtyHost::write( const char* buf, size_t len )
{
  size_t written = 0;
  while ( written < len ) {
    DWORD n = 0;
    if ( !WriteFile( impl->to_child, buf + written, static_cast<DWORD>( len - written ), &n, NULL ) || n == 0 ) {
      fprintf( stderr, "mosh-server: write to pty: %s\n", win_error( GetLastError() ).c_str() );
      return -1;
    }
    written += n;
  }
  return 0;
}

bool PtyHost::resize( int width, int height )
{
  if ( impl->pty == NULL ) {
    return false;
  }
  COORD size;
  size.X = static_cast<SHORT>( width );
  size.Y = static_cast<SHORT>( height );
  const HRESULT hr = ResizePseudoConsole( impl->pty, size );
  if ( FAILED( hr ) ) {
    fprintf( stderr, "mosh-server: ResizePseudoConsole failed (0x%08lx)\n", static_cast<unsigned long>( hr ) );
    return false;
  }
  return true;
}

void PtyHost::release( void )
{
  /* The child was never held back: there is no fork here, so it has been
     running its own login session since start(). */
}

void PtyHost::close( void )
{
  if ( impl->pty ) {
    /* Closes the pty and, with it, the child's console. This is what makes
       the shell see end of input and exit. */
    ClosePseudoConsole( impl->pty );
    impl->pty = NULL;
  }
  if ( impl->to_child != INVALID_HANDLE_VALUE ) {
    CloseHandle( impl->to_child );
    impl->to_child = INVALID_HANDLE_VALUE;
  }
  if ( impl->from_child != INVALID_HANDLE_VALUE ) {
    CloseHandle( impl->from_child );
    impl->from_child = INVALID_HANDLE_VALUE;
  }
  if ( impl->process ) {
    /* Give the shell a moment to notice its console has gone before the
       process handle is dropped. */
    WaitForSingleObject( impl->process, 2000 );
    CloseHandle( impl->process );
    impl->process = NULL;
    impl->child_gone = true;
  }
}
