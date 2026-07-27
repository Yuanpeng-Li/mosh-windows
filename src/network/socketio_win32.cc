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
#include <cstdio>
#include <cstring>

#include <windows.h>

#ifndef IP_ECN
#define IP_ECN 50
#endif
#ifndef IP_RECVECN
#define IP_RECVECN 50
#endif
#ifndef IPV6_ECN
#define IPV6_ECN 50
#endif
#ifndef IPV6_RECVECN
#define IPV6_RECVECN 50
#endif
#ifndef IP_MTU_DISCOVER
#define IP_MTU_DISCOVER 71
#endif
#ifndef IP_PMTUDISC_NOT_SET
#define IP_PMTUDISC_NOT_SET 0
#endif
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW( IOC_VENDOR, 12 )
#endif
#ifndef SIO_UDP_NETRESET
#define SIO_UDP_NETRESET _WSAIOW( IOC_VENDOR, 15 )
#endif

namespace Network {

SocketSubsystem::SocketSubsystem()
{
  WSADATA wsa;
  const int rc = WSAStartup( MAKEWORD( 2, 2 ), &wsa );
  if ( rc != 0 ) {
    fprintf( stderr, "WSAStartup failed: %d\n", rc );
    /* Nothing works without this and it fails only if Winsock is broken, so
       there is no useful recovery. */
    abort();
  }
}

SocketSubsystem::~SocketSubsystem()
{
  WSACleanup();
}

/* WSARecvMsg is not exported; it is reached through a per-provider function
   pointer. One lookup serves every socket of the same provider, which for
   mosh's plain UDP sockets is all of them. */
static LPFN_WSARECVMSG get_wsarecvmsg( socket_t s )
{
  static LPFN_WSARECVMSG fn = NULL;
  if ( fn ) {
    return fn;
  }
  GUID guid = WSAID_WSARECVMSG;
  DWORD nb = 0;
  if ( WSAIoctl( s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof guid, &fn, sizeof fn, &nb, NULL, NULL )
       != 0 ) {
    fn = NULL;
  }
  return fn;
}

socket_t udp_socket( int family )
{
  const socket_t s = socket( family, SOCK_DGRAM, IPPROTO_UDP );
  if ( s == INVALID_SOCKET ) {
    return BAD_SOCKET;
  }

  /* The one that is not optional.
   *
   * Windows surfaces an ICMP port-unreachable as WSAECONNRESET on the *next*
   * receive from an unconnected UDP socket -- a case POSIX mosh never meets,
   * since it never calls connect(). Without this, starting the client before
   * the server makes every wakeup throw, and mosh spins at 100% CPU while
   * refusing to connect. SIO_UDP_NETRESET is the same story for a routing
   * change. */
  {
    BOOL off = FALSE;
    DWORD ret = 0;
    WSAIoctl( s, SIO_UDP_CONNRESET, &off, sizeof off, NULL, 0, &ret, NULL, NULL );
    WSAIoctl( s, SIO_UDP_NETRESET, &off, sizeof off, NULL, 0, &ret, NULL, NULL );
  }

  /* Non-blocking is a property of the socket here; there is no MSG_DONTWAIT. */
  {
    u_long nonblocking = 1;
    if ( ioctlsocket( s, FIONBIO, &nonblocking ) != 0 ) {
      const int saved = WSAGetLastError();
      closesocket( s );
      WSASetLastError( saved );
      return BAD_SOCKET;
    }
  }

  const int level = ( family == AF_INET6 ) ? IPPROTO_IPV6 : IPPROTO_IP;

  /* Ask for ECN on send and to be told it on receive. Available from Windows
     10; older builds simply refuse the option, which costs only the congestion
     signal. Note that IP_TOS is accepted and then ignored by the stack unless
     a registry override is set, so DSCP marking does not happen on Windows and
     is not attempted. */
  {
    DWORD ecn = 1; /* ECT(0) */
    setsockopt( s, level, ( family == AF_INET6 ) ? IPV6_ECN : IP_ECN, (const char*)&ecn, sizeof ecn );
    DWORD on = 1;
    setsockopt( s, level, ( family == AF_INET6 ) ? IPV6_RECVECN : IP_RECVECN, (const char*)&on, sizeof on );
  }

  /* Path-MTU discovery, matching the POSIX side's IP_PMTUDISC_DONT: let the
     datagram be fragmented rather than dropped. */
  if ( family != AF_INET6 ) {
    DWORD mtu = IP_PMTUDISC_NOT_SET;
    setsockopt( s, IPPROTO_IP, IP_MTU_DISCOVER, (const char*)&mtu, sizeof mtu );
  }

  return s;
}

void close_socket( socket_t s )
{
  if ( s != BAD_SOCKET ) {
    closesocket( s );
  }
}

bool set_dual_stack( socket_t s )
{
  /* Windows defaults IPV6_V6ONLY to 1, where POSIX usually defaults it to 0.
     Without this a v6 bind would not accept v4-mapped traffic at all. */
  DWORD off = 0;
  return setsockopt( s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof off ) == 0;
}

int last_socket_error( void )
{
  return WSAGetLastError();
}

bool err_is_would_block( int e )
{
  return e == WSAEWOULDBLOCK;
}

bool err_is_msgsize( int e )
{
  return e == WSAEMSGSIZE;
}

bool err_is_transient_recv( int e )
{
  /* Belt and braces for the ioctls above: if a layered service provider
     refused them, these still arrive, and they describe one datagram rather
     than a broken socket. */
  return e == WSAEWOULDBLOCK || e == WSAECONNRESET || e == WSAENETRESET;
}

std::string socket_strerror( int e )
{
  char buf[256] = { 0 };
  FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e, 0, buf, sizeof buf, NULL );
  size_t n = strlen( buf );
  while ( n > 0 && ( buf[n - 1] == '\n' || buf[n - 1] == '\r' ) ) {
    buf[--n] = '\0';
  }
  if ( n == 0 ) {
    snprintf( buf, sizeof buf, "Winsock error %d", e );
  }
  return std::string( buf );
}

ssize_t udp_send( socket_t s, const void* buf, size_t len, const struct sockaddr* to, socklen_type tolen )
{
  const int n = sendto( s, (const char*)buf, (int)len, 0, to, tolen );
  return ( n == SOCKET_ERROR ) ? -1 : n;
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

  LPFN_WSARECVMSG WSARecvMsgPtr = get_wsarecvmsg( s );
  if ( !WSARecvMsgPtr ) {
    /* No control messages, so no ECN; the transport still works. */
    const int n = recvfrom( s, (char*)buf, (int)len, 0, from, fromlen );
    if ( n == SOCKET_ERROR ) {
      if ( WSAGetLastError() == WSAEMSGSIZE ) {
        *truncated = true;
      }
      return -1;
    }
    return n;
  }

  WSABUF wsabuf;
  wsabuf.buf = (CHAR*)buf;
  wsabuf.len = (ULONG)len;

  char control[1024];
  WSAMSG msg;
  memset( &msg, 0, sizeof msg );
  msg.name = from;
  msg.namelen = fromlen ? *fromlen : 0;
  msg.lpBuffers = &wsabuf;
  msg.dwBufferCount = 1;
  msg.Control.buf = control;
  msg.Control.len = sizeof control;
  msg.dwFlags = 0;

  DWORD received = 0;
  if ( WSARecvMsgPtr( s, &msg, &received, NULL, NULL ) == SOCKET_ERROR ) {
    if ( WSAGetLastError() == WSAEMSGSIZE ) {
      *truncated = true;
    }
    return -1;
  }

  if ( fromlen ) {
    *fromlen = msg.namelen;
  }
  if ( msg.dwFlags & MSG_TRUNC ) {
    *truncated = true;
  }

  for ( WSACMSGHDR* c = WSA_CMSG_FIRSTHDR( &msg ); c != NULL; c = WSA_CMSG_NXTHDR( &msg, c ) ) {
    const bool is_ecn = ( c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_ECN )
                        || ( c->cmsg_level == IPPROTO_IPV6 && c->cmsg_type == IPV6_ECN );
    if ( is_ecn && c->cmsg_len >= WSA_CMSG_LEN( sizeof( INT ) ) ) {
      INT ecn = 0;
      memcpy( &ecn, WSA_CMSG_DATA( c ), sizeof ecn );
      *congestion_experienced = ( ( ecn & 0x03 ) == 0x03 );
      break;
    }
  }

  return (ssize_t)received;
}

}
