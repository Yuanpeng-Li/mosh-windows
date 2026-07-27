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

#include "src/network/socketio.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#if HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

/* MSG_DONTWAIT is not in POSIX; some platforms spell it MSG_NONBLOCK. This
   moved here from network.cc along with everything else that touches the
   sockets API. */
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT MSG_NONBLOCK
#endif

namespace Network {

SocketSubsystem::SocketSubsystem() {}
SocketSubsystem::~SocketSubsystem() {}

socket_t udp_socket( int family )
{
  const socket_t s = socket( family, SOCK_DGRAM, 0 );
  if ( s < 0 ) {
    return BAD_SOCKET;
  }

#ifdef HAVE_IP_MTU_DISCOVER
  /* Ask for path-MTU discovery so a datagram is dropped with an ICMP reply
     rather than fragmented. */
  const int flag = IP_PMTUDISC_DONT;
  if ( setsockopt( s, IPPROTO_IP, IP_MTU_DISCOVER, &flag, sizeof flag ) < 0 ) {
    const int saved = errno;
    close( s );
    errno = saved;
    return BAD_SOCKET;
  }
#endif

  /* Request ECN-capable transport, and ask to be told the received TOS byte.
     Both are best-effort: a kernel or a middlebox that ignores them costs
     nothing but the congestion signal. */
  const int dscp = 0x02; /* ECT(0) */
  if ( setsockopt( s, IPPROTO_IP, IP_TOS, &dscp, sizeof dscp ) < 0 ) {
    /* Not fatal. */
  }

#ifdef HAVE_IP_RECVTOS
  const int tosflag = true;
  if ( setsockopt( s, IPPROTO_IP, IP_RECVTOS, &tosflag, sizeof tosflag ) < 0 && family == IPPROTO_IP ) {
    /* FreeBSD disallows this option on IPv6 sockets. */
    perror( "setsockopt( IP_RECVTOS )" );
  }
#endif

  return s;
}

void close_socket( socket_t s )
{
  if ( s != BAD_SOCKET ) {
    close( s );
  }
}

bool set_dual_stack( socket_t s )
{
  const int off = 0;
  return setsockopt( s, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off ) == 0;
}

int last_socket_error( void )
{
  return errno;
}

bool err_is_would_block( int e )
{
  return e == EAGAIN || e == EWOULDBLOCK;
}

bool err_is_msgsize( int e )
{
  return e == EMSGSIZE;
}

bool err_is_transient_recv( int e )
{
  /* On POSIX an unconnected UDP socket does not report ICMP errors at all, so
     there is nothing here beyond the would-block case the caller already
     handles. Kept so the receive loop reads the same on both platforms. */
  return err_is_would_block( e );
}

std::string socket_strerror( int e )
{
  return std::string( strerror( e ) );
}

ssize_t udp_send( socket_t s, const void* buf, size_t len, const struct sockaddr* to, socklen_type tolen )
{
  return sendto( s, buf, len, MSG_DONTWAIT, to, tolen );
}

ssize_t udp_recv( socket_t s,
                  void* buf,
                  size_t len,
                  struct sockaddr* from,
                  socklen_type* fromlen,
                  bool* congestion_experienced,
                  bool* truncated )
{
  *congestion_experienced = false;
  *truncated = false;

  struct msghdr header;
  struct iovec msg_iovec;
  char msg_control[1500];

  header.msg_name = from;
  header.msg_namelen = fromlen ? *fromlen : 0;

  msg_iovec.iov_base = buf;
  msg_iovec.iov_len = len;
  header.msg_iov = &msg_iovec;
  header.msg_iovlen = 1;

  header.msg_control = msg_control;
  header.msg_controllen = sizeof msg_control;
  header.msg_flags = 0;

  const ssize_t received_len = recvmsg( s, &header, MSG_DONTWAIT );
  if ( received_len < 0 ) {
    return -1;
  }

  if ( fromlen ) {
    *fromlen = header.msg_namelen;
  }
  if ( header.msg_flags & MSG_TRUNC ) {
    *truncated = true;
  }

  /* ECN, if the kernel told us the received TOS byte. */
  struct cmsghdr* ecn_hdr = CMSG_FIRSTHDR( &header );
  if ( ecn_hdr && ecn_hdr->cmsg_level == IPPROTO_IP
       && ( ecn_hdr->cmsg_type == IP_TOS
#ifdef IP_RECVTOS
            || ecn_hdr->cmsg_type == IP_RECVTOS
#endif
            ) ) {
    const uint8_t* ecn_octet_p = (const uint8_t*)CMSG_DATA( ecn_hdr );
    assert( ecn_octet_p );
    *congestion_experienced = ( *ecn_octet_p & 0x03 ) == 0x03;
  }

  return received_len;
}

}
