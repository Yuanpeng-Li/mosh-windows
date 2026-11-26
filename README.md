# Mosh for Windows

[![Windows build and tests](https://github.com/Yuanpeng-Li/mosh-windows/actions/workflows/windows.yml/badge.svg?branch=windows)](https://github.com/Yuanpeng-Li/mosh-windows/actions/workflows/windows.yml)
[![Latest Windows release](https://img.shields.io/github/v/release/Yuanpeng-Li/mosh-windows?label=Windows%20release&color=147d76)](https://github.com/Yuanpeng-Li/mosh-windows/releases/latest)
[![License](https://img.shields.io/badge/license-GPL--3.0%2B-426079)](COPYING)

Close your laptop on Wi-Fi, open it on a hotspot, and keep typing in the same remote shell. This native Windows port of Mosh lets you **connect from PowerShell or host a shell on your Windows PC**. On slow links, Mosh displays keystrokes locally when it can predict how the remote terminal will respond.

![A PowerShell terminal running mosh dev@lab. SSH handles login, then UDP carries the roaming session.](docs/images/windows-session.svg)

**[Download for Windows x64](https://github.com/Yuanpeng-Li/mosh-windows/releases/latest)** · [Host a Windows session](README.windows.md#connecting-in) · [Troubleshooting](README.windows.md#troubleshooting)

## Your first session

1. Download the **x64 ZIP** from the latest release. Extract the `mosh` folder and open PowerShell in it. The ZIP includes the launcher, scripts, client, and server. Windows 10 version 1809 or later and Windows 11 support the native terminal.
2. Try a session on your own machine. At the new shell prompt, type `exit` to return.

   ```powershell
   .\mosh.exe --local localhost
   ```

3. Connect to a remote host that has SSH and `mosh-server` installed. The server needs inbound UDP on the selected port. Mosh picks a port from **60000–61000** by default.

   ```powershell
   .\mosh.exe dev@lab
   ```

To run `mosh` from any directory, use the [per-user installer](README.windows.md#install). It adds Mosh to your user PATH and includes an uninstaller.

## Which machine needs what?

| Connection | What to set up |
| --- | --- |
| **Windows → Linux or macOS** | Install this package on Windows and your distribution's Mosh package on the remote host. SSH aliases work with the default address discovery. |
| **Windows → Windows** | Install this port on both machines. Enable OpenSSH Server and allow inbound UDP for `mosh-server.exe` on the host. [Windows host setup](README.windows.md#connecting-in) |
| **Linux or macOS → Windows** | Keep your packaged Mosh client. Connect with `mosh --no-ssh-pty user@windows-box`, or install the [launcher that handles Windows automatically](README.windows.md#your-linux-or-macos-client-needs-one-thing). |

For a firewall with one allowed UDP port, choose it explicitly. SSH keeps its own port setting.

```powershell
mosh -p 60010 dev@lab
mosh --ssh="ssh -p 2222" dev@lab
mosh dev@lab -- tmux attach
```

## Updates and release downloads

Starting with the **first release after v0.2.0**, the launcher can check for and install stable updates. Run these commands from a PATH installation, or use `.\mosh.exe` in the extracted folder.

```powershell
mosh --version
mosh update --check
mosh update
```

The updater verifies the release ZIP's SHA-256 and updates the programs and scripts together. If you have **v0.2.0 or earlier**, use the installer or extract a newer ZIP once to get the update command. WinGet-managed installations can also use `winget upgrade --id YuanpengLi.MoshWindows --exact`.

| Release download | Use it for |
| --- | --- |
| **mosh-windows-x64-vX.Y.Z.zip** | A complete installation. Start here. |
| **mosh.exe** | The launcher. Use it with the matching scripts from the ZIP. |
| **mosh-client.exe** and **mosh-server.exe** | The native client and server, available individually for packaging and integration. |
| **SHA256SUMS** | Verify a download with `Get-FileHash -Algorithm SHA256 <file>`. |

The individual executables and checksum file are included from the first release after v0.2.0. [Browse releases](https://github.com/Yuanpeng-Li/mosh-windows/releases)

## Build or contribute

The Windows build uses MSVC, CMake, vcpkg, and the Windows pseudoconsole (ConPTY). The test suite covers terminal and protocol behavior as well as Windows loopback sessions. For bug reports, include your Windows build, `mosh --version`, and which machine was the client. [Windows build instructions](README.windows.md#build-it-yourself) · [CMake options](docs/cmake-build.md) · [Report an issue](https://github.com/Yuanpeng-Li/mosh-windows/issues)

Mosh was created by Keith Winstein and the Mosh developers. This independent fork adds native Windows support and retains the upstream GPLv3-or-later license and OpenSSL linking exception. [Upstream Mosh](https://mosh.org/) · [Source](https://github.com/mobile-shell/mosh) · [License](COPYING)
