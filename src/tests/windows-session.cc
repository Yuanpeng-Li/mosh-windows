// Exercise the installed launcher, client and server through a real ConPTY.
// The SSH stand-in runs the remote command with a stock Windows command
// interpreter and a loopback SSH_CONNECTION, without configuring runner sshd.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <windows.h>

#include "src/util/ptyhost.h"
#include "src/util/select.h"

int main( int argc, char* argv[] )
{
  if ( argc == 3 && std::string( argv[1] ) == "emit" ) {
    if ( std::string( argv[2] ) != "argument with spaces" ) {
      return 1;
    }
    for ( int i = 0; i < 2000; i++ ) {
      puts( "output before the final line" );
    }
    puts( "MOSH-FINAL-FRAME" );
    return 0;
  }

  if ( argc >= 3 && std::string( argv[1] ) == "ssh" ) {
    const std::vector<std::string> options( argv + 2, argv + argc - 1 );
    if ( argv[argc - 2] == std::string( "localhost" )
         && std::find( options.begin(), options.end(), "HostName=127.0.0.1" ) == options.end() ) {
      return 2;
    }
    const std::string remote = argv[argc - 1];
    if ( remote == "sh -s" ) {
      return 1; // Stock Windows has no POSIX shell.
    }
    if ( remote != "powershell.exe -NoLogo -NoProfile -NonInteractive -Command -" ) {
      return 2;
    }
    _putenv_s( "SSH_CONNECTION", "127.0.0.1 4242 127.0.0.1 22" );
    return std::system( remote.c_str() );
  }

  if ( argc != 5 ) {
    return 2;
  }
  const std::string mode = argv[1];
  std::vector<std::string> command = { argv[2], std::string( "--client=" ) + argv[3],
                                       std::string( "--server=" ) + argv[4] };
  if ( mode == "local" ) {
    command.push_back( "--local" );
  } else {
    command.push_back( std::string( "--ssh=\"" ) + argv[0] + "\" ssh" );
    if ( mode == "dns" ) {
      command.push_back( "--experimental-remote-ip=local" );
      command.push_back( "-4" );
    }
  }
  // The remote-address case must use SSH_CONNECTION; this alias has no DNS.
  command.push_back( mode == "remote" ? "test-ssh-alias.invalid" : "localhost" );
  command.insert( command.end(), { "--", argv[0], "emit", "argument with spaces" } );

  PtyHost console;
  if ( !console.start( command, 80, 24 ) ) {
    return 1;
  }
  console.release();
  Select& sel = Select::get_instance();
  std::string screen;
  for ( ;; ) {
    sel.clear_fds();
    console.add_to( sel );
    if ( sel.select( 1000 ) < 0 || !console.finish_if_exited( sel ) ) {
      return 1;
    }
    if ( console.readable( sel ) ) {
      char buf[8192];
      const ssize_t n = console.read( buf, sizeof buf );
      if ( n <= 0 ) {
        break;
      }
      screen.append( buf, n );
    }
  }
  if ( screen.find( "MOSH-FINAL-FRAME" ) == std::string::npos ) {
    fputs( "client exited without displaying the final frame\n", stderr );
    return 1;
  }
  return 0;
}
