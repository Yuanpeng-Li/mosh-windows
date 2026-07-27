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
