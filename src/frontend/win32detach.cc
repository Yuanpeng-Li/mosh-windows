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

#include "src/frontend/win32detach.h"

#include <cstdio>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "src/include/version.h"

namespace {

const wchar_t DAEMON_ENV[] = L"MOSH_WIN_DETACHED";

std::string narrow( const std::wstring& w )
{
  if ( w.empty() ) {
    return std::string();
  }
  const int need = WideCharToMultiByte( CP_UTF8, 0, w.data(), static_cast<int>( w.size() ), NULL, 0, NULL, NULL );
  std::string out( need > 0 ? need : 0, '\0' );
  if ( need > 0 ) {
    WideCharToMultiByte( CP_UTF8, 0, w.data(), static_cast<int>( w.size() ), &out[0], need, NULL, NULL );
  }
  return out;
}

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

bool find_on_path( const wchar_t* name, std::string& out )
{
  wchar_t buf[MAX_PATH];
  const DWORD n = SearchPathW( NULL, name, NULL, MAX_PATH, buf, NULL );
  if ( n == 0 || n >= MAX_PATH ) {
    return false;
  }
  out = narrow( std::wstring( buf, n ) );
  return true;
}

}

namespace Win32Detach {

bool is_detached( void )
{
  /* Read once, then clear, then answer from the cache.

     Clearing matters: the daemon inherited this from the process that spawned
     it, and everything the daemon starts -- the shell, and whatever the user
     runs in it -- inherits the daemon's environment in turn, because
     ptyhost_win32 passes lpEnvironment = NULL. Left set, a mosh-server started
     from inside a mosh session sees it, believes it is already the detached
     copy, skips spawn(), and dies with its parent: `mosh --local` from inside
     a session, or mosh from A to B to C, failing with no diagnostic.

     Caching matters because clearing makes the raw query answer differently
     the second time. There is one caller today; this keeps a second one from
     being a trap that spawns a second daemon. */
  static int cached = -1;
  if ( cached < 0 ) {
    cached = ( GetEnvironmentVariableW( DAEMON_ENV, NULL, 0 ) != 0 ) ? 1 : 0;
    if ( cached ) {
      SetEnvironmentVariableW( DAEMON_ENV, NULL );
    }
  }
  return cached != 0;
}

int spawn( void )
{
  /* The daemon is this same program with this same command line -- and "this
     same program" means this image on disk, not whatever the first word of the
     command line resolves to.

     With lpApplicationName NULL, CreateProcess parses lpCommandLine for the
     executable and searches for it; that search includes the current directory.
     The first word here is what ssh sent, which is the bare word `mosh-server`,
     and the working directory of a command run by sshd is the user's home. A
     mosh-server.exe sitting there -- dropped by anything that can write to the
     home directory -- would be started instead of this one, with this one's
     arguments. Naming the image explicitly closes that; the command line is
     still passed, so the daemon parses the same argv it would have. */
  wchar_t self[MAX_PATH];
  const DWORD self_len = GetModuleFileNameW( NULL, self, MAX_PATH );
  if ( self_len == 0 || self_len >= MAX_PATH ) {
    fprintf( stderr, "mosh-server: GetModuleFileName: %s\n", win_error( GetLastError() ).c_str() );
    return -1;
  }


  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof sa;
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = TRUE;

  HANDLE read_end = NULL, write_end = NULL;
  if ( !CreatePipe( &read_end, &write_end, &sa, 0 ) ) {
    fprintf( stderr, "mosh-server: CreatePipe: %s\n", win_error( GetLastError() ).c_str() );
    return -1;
  }
  /* Our end must not reach the daemon, or the read below never sees the end
     of the stream. */
  SetHandleInformation( read_end, HANDLE_FLAG_INHERIT, 0 );

  SIZE_T attr_size = 0;
  InitializeProcThreadAttributeList( NULL, 1, 0, &attr_size );
  std::vector<char> attr_buf( attr_size );
  LPPROC_THREAD_ATTRIBUTE_LIST attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>( &attr_buf[0] );
  HANDLE inherit_only = write_end;
  if ( !InitializeProcThreadAttributeList( attrs, 1, 0, &attr_size )
       || !UpdateProcThreadAttribute(
         attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inherit_only, sizeof inherit_only, NULL, NULL ) ) {
    /* Without this the daemon inherits every inheritable handle sshd left
       open, including sshd's own stdout -- and then ssh never finishes
       disconnecting, because a handle to its pipe is still open. */
    fprintf( stderr, "mosh-server: UpdateProcThreadAttribute: %s\n", win_error( GetLastError() ).c_str() );
    CloseHandle( read_end );
    CloseHandle( write_end );
    return -1;
  }

  STARTUPINFOEXW si;
  memset( &si, 0, sizeof si );
  si.StartupInfo.cb = sizeof si;
  si.lpAttributeList = attrs;
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = NULL;
  si.StartupInfo.hStdOutput = write_end;
  si.StartupInfo.hStdError = write_end;

  SetEnvironmentVariableW( DAEMON_ENV, L"1" );

  std::wstring cmdline( GetCommandLineW() );
  std::vector<wchar_t> mutable_cmdline( cmdline.begin(), cmdline.end() );
  mutable_cmdline.push_back( L'\0' );

  const DWORD base_flags = EXTENDED_STARTUPINFO_PRESENT | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;

  PROCESS_INFORMATION pi;
  memset( &pi, 0, sizeof pi );
  bool broke_away = true;
  BOOL ok = CreateProcessW(
    self, &mutable_cmdline[0], NULL, NULL, TRUE, base_flags | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL,
    &si.StartupInfo, &pi );
  if ( !ok && GetLastError() == ERROR_ACCESS_DENIED ) {
    /* BREAKAWAY_OK is a property of the job sshd happens to create, not a
       promise. Without it the session still works, but only until ssh
       disconnects -- which is worth saying out loud rather than leaving the
       user to discover. */
    broke_away = false;
    ok = CreateProcessW(
      self, &mutable_cmdline[0], NULL, NULL, TRUE, base_flags, NULL, NULL, &si.StartupInfo, &pi );
  }
  const DWORD spawn_error = GetLastError();

  DeleteProcThreadAttributeList( attrs );
  SetEnvironmentVariableW( DAEMON_ENV, NULL );
  /* Let go of the daemon's end, so reading below reaches the end of the
     stream when the daemon closes it. */
  CloseHandle( write_end );

  if ( !ok ) {
    fprintf( stderr, "mosh-server: cannot start the detached server: %s\n", win_error( spawn_error ).c_str() );
    CloseHandle( read_end );
    return -1;
  }
  CloseHandle( pi.hThread );

  /* Relay what the daemon says until it hands the pipe back. */
  std::string line, connect_line, other;
  bool got_connect = false;
  for ( ;; ) {
    char buf[512];
    DWORD n = 0;
    if ( !ReadFile( read_end, buf, sizeof buf, &n, NULL ) || n == 0 ) {
      break;
    }
    line.append( buf, n );
    size_t nl;
    while ( ( nl = line.find( '\n' ) ) != std::string::npos ) {
      std::string one = line.substr( 0, nl );
      line.erase( 0, nl + 1 );
      if ( !one.empty() && one[one.size() - 1] == '\r' ) {
        one.erase( one.size() - 1 );
      }
      if ( !got_connect && one.compare( 0, 13, "MOSH CONNECT " ) == 0 ) {
        connect_line = one;
        got_connect = true;
      } else if ( !one.empty() ) {
        other += one;
        other += '\n';
      }
    }
    if ( got_connect ) {
      /* Everything after this is the daemon's own log; it has already
         redirected it, and waiting for more would wait for the session. */
      break;
    }
  }
  CloseHandle( read_end );

  if ( !other.empty() ) {
    fputs( other.c_str(), stderr );
  }

  if ( !got_connect ) {
    DWORD status = 0;
    GetExitCodeProcess( pi.hProcess, &status );
    fprintf( stderr, "mosh-server: the detached server exited without connecting (status %lu).\n",
             static_cast<unsigned long>( status ) );
    CloseHandle( pi.hProcess );
    return -1;
  }

  printf( "%s\n", connect_line.c_str() );
  fflush( stdout );

  fputs( "\nmosh-server (" PACKAGE_STRING ") [build " BUILD_VERSION "]\n"
         "Copyright 2012 Keith Winstein <mosh-devel@mit.edu>\n"
         "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
         "This is free software: you are free to change and redistribute it.\n"
         "There is NO WARRANTY, to the extent permitted by law.\n\n",
         stderr );
  fprintf( stderr, "[mosh-server detached, pid = %lu]\n", static_cast<unsigned long>( pi.dwProcessId ) );
  if ( !broke_away ) {
    fputs( "\nWarning: could not leave this session's job object.\n"
           "The server will be killed when this ssh connection closes, which\n"
           "defeats the point of mosh. Reconnecting will not work.\n",
           stderr );
  }
  fflush( stderr );

  const int pid = static_cast<int>( pi.dwProcessId );
  CloseHandle( pi.hProcess );
  return pid;
}

void release_stdio( unsigned int verbose )
{
  std::string log_path;
  if ( verbose ) {
    wchar_t dir[MAX_PATH];
    const DWORD n = GetTempPathW( MAX_PATH, dir );
    if ( n > 0 && n < MAX_PATH ) {
      char name[64];
      snprintf( name, sizeof name, "mosh-server-%lu.log", static_cast<unsigned long>( GetCurrentProcessId() ) );
      log_path = narrow( std::wstring( dir, n ) ) + name;
      /* Say where it went while the launcher is still listening. */
      printf( "[mosh-server logging to %s]\n", log_path.c_str() );
      fflush( stdout );
    }
  }

  const char* target = log_path.empty() ? "NUL" : log_path.c_str();
  if ( freopen( target, "a", stderr ) == NULL ) {
    freopen( "NUL", "a", stderr );
  }
  if ( freopen( target, "a", stdout ) == NULL ) {
    freopen( "NUL", "a", stdout );
  }
  freopen( "NUL", "r", stdin );

  /* freopen rebinds the CRT stream; the process's own standard handles still
     point at the launcher's pipe, and ssh waits on every last one of them. */
  const HANDLE null_out = CreateFileW( L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                       OPEN_EXISTING, 0, NULL );
  if ( null_out != INVALID_HANDLE_VALUE ) {
    SetStdHandle( STD_OUTPUT_HANDLE, null_out );
    SetStdHandle( STD_ERROR_HANDLE, null_out );
    SetStdHandle( STD_INPUT_HANDLE, NULL );
  }
}

std::vector<std::string> default_shell( void )
{
  std::vector<std::string> argv;
  std::string path;

  /* PowerShell 7 is a separate product from Windows PowerShell and is what a
     user who has installed it means by "my shell". */
  if ( find_on_path( L"pwsh.exe", path ) ) {
    argv.push_back( path );
    argv.push_back( "-NoLogo" );
    return argv;
  }
  if ( find_on_path( L"powershell.exe", path ) ) {
    argv.push_back( path );
    argv.push_back( "-NoLogo" );
    return argv;
  }

  wchar_t comspec[MAX_PATH];
  const DWORD n = GetEnvironmentVariableW( L"ComSpec", comspec, MAX_PATH );
  argv.push_back( ( n > 0 && n < MAX_PATH ) ? narrow( std::wstring( comspec, n ) ) : "cmd.exe" );
  return argv;
}

}
