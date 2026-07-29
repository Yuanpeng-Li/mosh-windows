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

#ifndef PTYHOST_HPP
#define PTYHOST_HPP

#include "src/util/compat.h"

#include <string>
#include <vector>

class Select;

/* The pty the served command runs on, and the command itself.
 *
 * The two platforms do not divide this work the same way, and the interface
 * does not pretend otherwise.
 *
 * On POSIX, mosh-server forks before it has a pty: forkpty() produces the
 * master and the child then does its own login-session setup -- motd, TERM,
 * home directory, execvp -- none of which the parent can do on its behalf.
 * What is handed here is an already-open master plus the pipe that holds the
 * child back until a client has really connected; this class owns them from
 * that point.
 *
 * On Windows there is no fork. The pseudoconsole and the child are created
 * together, in one call, and there is no window in which a half-built child
 * exists, so there is nothing to hold back and release() does nothing.
 *
 * A single start()/adopt() pair that lied about this would have to invent a
 * child-setup callback that runs after fork on one platform and never runs on
 * the other, which is a worse thing to have to reason about than two names.
 */

class PtyHost
{
public:
  PtyHost();
  ~PtyHost();

  /* Registers whatever has to be waited on. On Windows that is the pty's
     output pipe *and* the child process: ConPTY does not close the pipe when
     the child exits, so a child that has gone is otherwise invisible. */
  void add_to( Select& sel ) const;

  /* Bytes are waiting from the command. */
  bool readable( const Select& sel ) const;

  /* The command has exited. Always false on POSIX, where the same event
     arrives as end of file on the master and is handled there. */
  bool exited( const Select& sel ) const;

  /* Returns the byte count, 0 at end of input, or -1 on error. The caller
     treats an error the same as end of input: with the pty slave closed, a
     read of the master fails with EIO rather than returning 0 (mosh#264). */
  ssize_t read( char* buf, size_t len );

  /* Writes the whole buffer. Returns 0, or -1 having reported the reason --
     the same contract as swrite(), which is what the caller checks. */
  int write( const char* buf, size_t len );

  bool resize( int width, int height );

  /* Lets the command start its login session, now that a client has really
     connected. Does nothing where the command was never held back. */
  void release( void );

  void close( void );

#if defined( _WIN32 )
  /* Creates a pseudoconsole of the given size and starts args on it.
     Returns false having reported why. */
  bool start( const std::vector<std::string>& args, int width, int height );
#else
  /* Takes ownership of a forkpty() master and the release pipe. */
  void adopt( int master_fd, int release_pipe_fd );

  /* utempter records a session against the master's file descriptor. */
  int master( void ) const;
#endif

private:
  PtyHost( const PtyHost& );
  PtyHost& operator=( const PtyHost& );

  struct Impl;
  Impl* impl;
};

#endif
