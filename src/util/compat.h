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

#ifndef MOSH_COMPAT_HPP
#define MOSH_COMPAT_HPP

/* Small compiler and platform spellings that are not worth a header each.
   Keep this cheap to include: no system headers, no dependencies. */

/* Marks a parameter that is deliberately unused. mosh spelled this as GCC's
   unused attribute directly, which MSVC rejects outright -- not as a warning,
   as a syntax error. */
#if defined( __GNUC__ ) || defined( __clang__ )
#define MOSH_UNUSED __attribute__( ( unused ) )
#else
#define MOSH_UNUSED
#endif

/* ssize_t is POSIX, not C or C++, and the MSVC CRT does not define it.
   SSIZE_T from <BaseTsd.h> is the same thing under another name. */
#if defined( _WIN32 ) && !defined( __MINGW32__ )
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

/* Signals POSIX has and Windows does not. mosh registers interest in these and
   the Windows Select accepts them as no-ops, so the call sites need no #ifdef.
   The values only have to be distinct and not collide with the MSVC CRT's own
   (SIGINT 2, SIGILL 4, SIGABRT 22, SIGFPE 8, SIGSEGV 11, SIGTERM 15,
   SIGBREAK 21); these are the familiar Linux numbers, which are all free. */
#if defined( _WIN32 )
#ifndef SIGHUP
#define SIGHUP 1
#endif
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGCONT
#define SIGCONT 18
#endif
#ifndef SIGWINCH
#define SIGWINCH 28
#endif
#ifndef SIGSTOP
#define SIGSTOP 19
#endif
#ifndef SIGUSR1
#define SIGUSR1 10
#endif
#endif

/* POSIX functions the MSVC CRT lacks. Implemented in compat_win32.cc. */
#if defined( _WIN32 ) && !defined( __MINGW32__ )
#include <cstddef>
#include <ctime>

extern "C" {
extern char* optarg;
extern int optind, opterr, optopt;
int getopt( int argc, char* const argv[], const char* optstring );
int unsetenv( const char* name );
int setenv( const char* name, const char* value, int overwrite );
int nanosleep( const struct timespec* req, struct timespec* rem );
}
#endif

/* What mosh writes bytes to. A small integer on POSIX; a kernel object handle
   on Windows, where neither the console nor a pipe has a file descriptor
   unless one is manufactured with _open_osfhandle -- and manufacturing one
   only to take it apart again in every write would be pure ceremony. */
#if defined( _WIN32 )
typedef void* mosh_fd_t; /* HANDLE */
#define MOSH_BAD_FD ( (mosh_fd_t)(long long)-1 )
#else
typedef int mosh_fd_t;
#define MOSH_BAD_FD ( -1 )
#endif

#endif
