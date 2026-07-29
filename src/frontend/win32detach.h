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

#ifndef WIN32DETACH_HPP
#define WIN32DETACH_HPP

#include <string>
#include <vector>

/* What fork() does for mosh-server on POSIX, done the way Windows can.
 *
 * mosh-server has to outlive the ssh session that started it. On POSIX it
 * forks and the parent exits. Windows has no fork, and worse: OpenSSH runs
 * session processes inside a job object marked kill-on-close, so an ordinary
 * child dies about a second after ssh disconnects no matter how it was
 * started. What survives is a process started with CREATE_BREAKAWAY_FROM_JOB.
 *
 * So the detached copy is a second run of this same program, told by an
 * environment variable that it is the daemon. It reports the port and key it
 * chose back over a pipe, which the launcher prints and then exits, exactly
 * as the POSIX parent prints what the fork already knew.
 */

namespace Win32Detach {

/* True in the detached copy. */
bool is_detached( void );

/* Launcher side: starts the detached copy, relays its MOSH CONNECT line to
   stdout and the banner to stderr. Returns the daemon's process id, or -1
   having reported why. */
int spawn( void );

/* Daemon side: lets go of the pipe the launcher is reading, so that ssh can
   finish disconnecting. With verbose set, what would have been written to it
   goes to a file under the temporary directory instead of being dropped, and
   the path is reported first. */
void release_stdio( unsigned int verbose );

/* The shell to run when the command line named none: PowerShell 7 if it is
   installed, Windows PowerShell if not, and the command processor failing
   both. */
std::vector<std::string> default_shell( void );

}

#endif
