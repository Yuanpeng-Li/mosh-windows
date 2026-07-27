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

#ifndef SWRITE_HPP
#define SWRITE_HPP

#include "src/util/compat.h"

/* Write the whole buffer, looping over short writes. Returns 0, or -1 having
   reported the reason.

   Every byte mosh displays and every byte it sends to the host goes through
   here, over exactly two kinds of destination: the local terminal, and the
   process at the far end of the pty. On POSIX both are file descriptors; on
   Windows the first is a console handle and the second a pipe handle, neither
   of which has a descriptor unless one is manufactured with _open_osfhandle.
   Hence mosh_fd_t rather than int. */
int swrite( mosh_fd_t fd, const char* str, ssize_t len = -1 );

/* Where the local terminal is: STDOUT_FILENO on POSIX, the standard output
   handle on Windows. Call sites named the constant directly, which is not
   spellable once the type is a handle. */
mosh_fd_t mosh_stdout_fd( void );

#endif
