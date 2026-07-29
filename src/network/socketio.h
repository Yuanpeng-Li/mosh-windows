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

#ifndef SOCKETIO_HPP
#define SOCKETIO_HPP

/* Every operating system call the UDP transport makes, behind one interface,
 * so that src/network/network.cc contains none of them.
 *
 * The BSD sockets API and Winsock look similar enough to tempt a few #ifdefs
 * and a typedef, and that would be wrong in ways that compile cleanly:
 *
 *   - errno and WSAGetLastError() are different namespaces with overlapping
 *     numbers. In the MSVC CRT EWOULDBLOCK is 140 while WSAEWOULDBLOCK is
 *     10035, so `errno == EWOULDBLOCK` after a socket call builds without a
 *     warning and is always false.
 *
 *   - Windows reports an ICMP port-unreachable as WSAECONNRESET on the next
 *     receive from an *unconnected* UDP socket. POSIX mosh never sees this,
 *     because it never calls connect(). Left alone, starting the client before
 *     the server makes every wakeup throw.
 *
 *   - a SOCKET is a handle, not a small integer, so it is not an index and not
 *     comparable to a file descriptor.
 *
 *   - MSG_DONTWAIT does not exist; non-blocking is a property set once on the
 *     socket.
 *
 *   - recvmsg's control-message layout is WSAMSG with its own macros.
 */

#include "src/include/config.h"
#include "src/util/compat.h"

#include <cstddef>
#include <string>

#if defined( _WIN32 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

namespace Network {

#if defined( _WIN32 )
typedef UINT_PTR socket_t;
static const socket_t BAD_SOCKET = static_cast<socket_t>( ~0 );
typedef int socklen_type;
#else
typedef int socket_t;
static const socket_t BAD_SOCKET = -1;
typedef socklen_t socklen_type;
#endif

/* Winsock needs WSAStartup before anything else and mosh has no other reason
   to know that. Constructed once, at the top of main. A no-op on POSIX. */
class SocketSubsystem
{
public:
  SocketSubsystem();
  ~SocketSubsystem();

private:
  SocketSubsystem( const SocketSubsystem& );
  SocketSubsystem& operator=( const SocketSubsystem& );
};

/* A UDP socket with everything mosh wants already applied: non-blocking, ECN
   requested where the platform offers it, path-MTU discovery where it exists,
   and -- on Windows -- SIO_UDP_CONNRESET turned off. Returns BAD_SOCKET on
   failure, with last_socket_error() set. */
socket_t udp_socket( int family );

void close_socket( socket_t s );

/* IPV6_V6ONLY off, for a dual-stack bind. Windows defaults it to 1, POSIX
   usually to 0, so mosh has to be explicit on both. */
bool set_dual_stack( socket_t s );

/* The last error from a socket call: errno on POSIX, WSAGetLastError() on
   Windows. Never mix these with errno from the C library. */
int last_socket_error( void );
bool err_is_would_block( int e );
bool err_is_msgsize( int e );

/* Errors that mean "this datagram, not this socket": a stale ICMP error
   surfacing on an unconnected UDP socket. The receive loop skips them rather
   than tearing the session down. */
bool err_is_transient_recv( int e );

std::string socket_strerror( int e );

/* Returns the number of bytes sent, or -1 with last_socket_error() set. */
ssize_t udp_send( socket_t s, const void* buf, size_t len, const struct sockaddr* to, socklen_type tolen );

/* Receives one datagram.
 *
 * Returns the number of bytes, or -1 with last_socket_error() set.
 * *congestion_experienced is set when the peer's ECN bits say so, and left
 * false when the platform cannot report them. *truncated is set when the
 * datagram was larger than the buffer. */
ssize_t udp_recv( socket_t s,
                  void* buf,
                  size_t len,
                  struct sockaddr* from,
                  socklen_type* fromlen,
                  bool* congestion_experienced,
                  bool* truncated );

}

#endif
