/* mosh-launcher.cc -- a native `mosh.exe` that runs mosh.ps1.
 *
 * The launcher itself is PowerShell, but a .ps1 is not a command: it cannot be
 * put on PATH and invoked as `mosh`, and winget's portable installer only
 * accepts .exe. This is the ~10 KB shim that makes `mosh` a real program.
 *
 * It runs:
 *     pwsh -NoLogo -NoProfile -Command "& '<dir>\mosh.ps1' @args" -- <argv...>
 *
 * -Command with the call operator rather than -File on purpose: -File's
 * argument parser splits "--client=C:\path" at the colon, reading it as the
 * -Name:Value parameter syntax. Falls back to powershell.exe when PowerShell 7
 * is not installed.
 *
 * Build: cl /nologo /EHsc /O2 /W3 mosh-launcher.cc /Fe:mosh.exe shell32.lib
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <shlwapi.h>

#include <cstdio>
#include <string>
#include <vector>

/* Quote one argument so that CommandLineToArgvW in the child reconstructs it
   byte for byte. The backslash rules are the fiddly part: backslashes are only
   special immediately before a quote. */
static std::wstring quote_arg( const std::wstring& arg )
{
  if ( !arg.empty() && arg.find_first_of( L" \t\n\v\"" ) == std::wstring::npos ) {
    return arg;
  }
  std::wstring out = L"\"";
  for ( size_t i = 0;; ++i ) {
    size_t backslashes = 0;
    while ( i < arg.size() && arg[i] == L'\\' ) {
      ++i;
      ++backslashes;
    }
    if ( i == arg.size() ) {
      out.append( backslashes * 2, L'\\' ); /* before the closing quote */
      break;
    } else if ( arg[i] == L'"' ) {
      out.append( backslashes * 2 + 1, L'\\' ); /* escape them and the quote */
      out.push_back( L'"' );
    } else {
      out.append( backslashes, L'\\' );
      out.push_back( arg[i] );
    }
  }
  out.push_back( L'"' );
  return out;
}

/* Single-quote for PowerShell's own parser, which only treats '' specially. */
static std::wstring ps_single_quote( const std::wstring& s )
{
  std::wstring out = L"'";
  for ( wchar_t c : s ) {
    if ( c == L'\'' ) {
      out += L"''";
    } else {
      out.push_back( c );
    }
  }
  out.push_back( L'\'' );
  return out;
}

static bool file_exists( const std::wstring& p )
{
  DWORD a = GetFileAttributesW( p.c_str() );
  return a != INVALID_FILE_ATTRIBUTES && !( a & FILE_ATTRIBUTE_DIRECTORY );
}

/* Resolve a bare program name against PATH. */
static std::wstring which( const wchar_t* exe )
{
  wchar_t buf[MAX_PATH * 4];
  DWORD n = SearchPathW( NULL, exe, NULL, ARRAYSIZE( buf ), buf, NULL );
  if ( n > 0 && n < ARRAYSIZE( buf ) ) {
    return buf;
  }
  return L"";
}

int wmain( int argc, wchar_t** argv )
{
  wchar_t selfPath[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW( NULL, selfPath, ARRAYSIZE( selfPath ) );
  if ( n == 0 || n >= ARRAYSIZE( selfPath ) ) {
    fwprintf( stderr, L"mosh: cannot determine my own path\n" );
    return 1;
  }
  std::wstring dir( selfPath );
  size_t slash = dir.find_last_of( L"\\/" );
  dir = ( slash == std::wstring::npos ) ? L"." : dir.substr( 0, slash );

  const std::wstring script = dir + L"\\mosh.ps1";
  if ( !file_exists( script ) ) {
    fwprintf( stderr, L"mosh: cannot find mosh.ps1 next to %ls\n", selfPath );
    return 1;
  }

  /* PowerShell 7 if present -- it is what mosh.ps1 is developed against --
     otherwise Windows PowerShell, which every Windows has. */
  std::wstring shell = which( L"pwsh.exe" );
  if ( shell.empty() ) {
    shell = which( L"powershell.exe" );
  }
  if ( shell.empty() ) {
    fwprintf( stderr, L"mosh: neither pwsh.exe nor powershell.exe found on PATH\n" );
    return 1;
  }

  /* Every argument is embedded in the -Command string, single-quoted for
     PowerShell's parser, and the whole string is then quoted once for
     CommandLineToArgvW. Two layers, because there are two parsers.
     Passing them after a `--` separator instead does not work: PowerShell
     re-tokenizes those, so `--client=C:\Program Files\x.exe` arrives split at
     the space. */
  std::wstring psCommand = L"& " + ps_single_quote( script );
  for ( int i = 1; i < argc; ++i ) {
    psCommand += L" ";
    psCommand += ps_single_quote( argv[i] );
  }

  std::wstring cmd = quote_arg( shell );
  cmd += L" -NoLogo -NoProfile -ExecutionPolicy Bypass -Command ";
  cmd += quote_arg( psCommand );

  STARTUPINFOW si;
  ZeroMemory( &si, sizeof si );
  si.cb = sizeof si;
  PROCESS_INFORMATION pi;
  ZeroMemory( &pi, sizeof pi );

  std::vector<wchar_t> mutableCmd( cmd.begin(), cmd.end() );
  mutableCmd.push_back( L'\0' );

  /* Inherit the console: mosh-client needs the real one, and ssh needs stdin
     for password and 2FA prompts. */
  if ( !CreateProcessW( NULL, mutableCmd.data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi ) ) {
    fwprintf( stderr, L"mosh: cannot start PowerShell (error %lu)\n", GetLastError() );
    return 1;
  }

  WaitForSingleObject( pi.hProcess, INFINITE );
  DWORD rc = 1;
  GetExitCodeProcess( pi.hProcess, &rc );
  CloseHandle( pi.hThread );
  CloseHandle( pi.hProcess );
  return (int)rc;
}
