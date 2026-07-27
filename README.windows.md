mosh for Windows
================

`mosh user@host`, natively on Windows. No Cygwin, no WSL, no MSYS2.

[Mosh](https://mosh.org) is a remote terminal that survives sleep, roaming and
packet loss, and echoes your typing locally so a laggy link still feels
responsive. It has never had a native Windows build. This fork is porting it.

Two halves, at very different stages:

| | |
|---|---|
| **Connecting out** — `mosh user@host` from Windows to a Linux/macOS server | **works today**, see below |
| **Connecting in** — `mosh` *into* Windows and get a PowerShell session | in progress; nothing like it exists yet |

---

Install
-------

### One line

```powershell
irm https://raw.githubusercontent.com/Yuanpeng-Li/mosh-windows/windows/scripts/install.ps1 | iex
```

Installs into `%LOCALAPPDATA%\Programs\mosh`, adds that one directory to your
**user** PATH, and downloads a `mosh-client.exe`. No admin, nothing written
anywhere else.

Then open a **new** terminal:

```powershell
mosh user@host
```

### winget

```powershell
winget install YuanpengLi.MoshWindows
mosh --setup          # fetches mosh-client.exe on first use
```

> The winget package contains the launcher only. It does not bundle
> `mosh-client.exe`, because that binary belongs to another project
> ([MoshCatty](https://github.com/binaricat/MoshCatty), GPLv3) and
> redistributing it would put the obligation to supply matching source on us.
> `mosh --setup` downloads it from its own release and checks it against a
> pinned SHA-256.

### By hand

Take `mosh.exe` and `mosh.ps1` out of the
[release archive](https://github.com/Yuanpeng-Li/mosh-windows/releases/latest),
put them in a directory on your PATH, and run `mosh --setup`.

`mosh.exe` is a small shim that finds `mosh.ps1` beside it and runs it under
PowerShell; it exists because a `.ps1` cannot be put on PATH and invoked as a
command. From a source checkout without a compiler, `mosh.cmd` does the same
job, with the caveat that batch cannot quote an argument containing a space for
PowerShell's parser.

### Uninstall

```powershell
mosh-uninstall
```

---

Use
---

Exactly as on Linux and macOS — the options are the same ones, spelled the
same way:

```powershell
mosh user@host                      # ssh_config aliases work too: mosh myserver
mosh -p 60000:60010 user@host       # pin the UDP port range
mosh --predict=always user@host     # local echo even on a fast link
mosh --ssh="ssh -p 2222" user@host  # non-standard ssh port
mosh user@host -- tmux attach       # run something instead of a login shell
mosh --help
```

Windows-only additions:

```powershell
mosh --locale=en_US.UTF-8 user@host  # Windows has no LANG; C.UTF-8 by default
mosh --setup                          # download and verify a mosh-client
```

Two differences from upstream, both reported by `--help`:

- `--experimental-remote-ip=proxy` is refused. It works by re-invoking the
  launcher as an ssh `ProxyCommand`, and Win32 OpenSSH runs `ProxyCommand`
  through `cmd.exe`, whose quoting is not `sh`'s. The default here is `remote`,
  which asks the server for its address via `$SSH_CONNECTION`.
- `--local` needs a native `mosh-server`, which does not exist yet.

### On the server

Just mosh, from your distribution — nothing from this project:

```sh
apt install mosh        # Debian, Ubuntu
dnf install mosh        # Fedora, RHEL
brew install mosh       # macOS
```

The server needs inbound **UDP 60000–61000** open. Most VPS firewalls block it
by default:

```sh
sudo ufw allow 60000:61000/udp
```

---

Troubleshooting
---------------

**`Did not find a 'MOSH CONNECT' line`**
`mosh-server` is not on the remote PATH, or the remote locale is not UTF-8, or
something in the remote shell's startup files printed first. Check with
`ssh user@host mosh-server --version`, and try `--locale=en_US.UTF-8`.

**`Nothing received from server on UDP port 60001`**
ssh worked and the server started, but its UDP replies are not arriving. Almost
always a firewall between you and the host. Note the port range, not just one
port — mosh hops ports while roaming.

**`mosh: Cannot find mosh-client`**
Run `mosh --setup`, or point `--client` at your own build.

**Chinese, Japanese or emoji are misaligned**
Client and server must agree on how wide each character is. If they disagree,
everything after the first wide character shifts. Please
[open an issue](https://github.com/Yuanpeng-Li/mosh-windows/issues) with the
exact text — this is the failure mode the native client is being built to fix.

**The prompt redraws oddly, or local echo never engages**
Try `mosh --predict=never user@host`. If that fixes it, the prediction engine is
disagreeing with your shell's redraw behaviour; an issue with your shell and
prompt would be useful.

---

Status
------

**Working now.** `mosh user@host` from Windows, with the full upstream
command-line interface. Verified end to end against a Linux host: ssh
bootstrap, session key handoff, live shell with colour and title.

The launcher is this project's code. The client it drives is currently
[MoshCatty](https://github.com/binaricat/MoshCatty), an independent Rust
reimplementation of the mosh protocol. It is not a port of mosh's own terminal
emulator, so its rendering may differ from real mosh in corners — wide
characters and heavy TUI applications are worth watching.

**Being built.**

- `mosh-client.exe` from mosh's actual C++ source, so behaviour matches upstream
  exactly. The launcher already prefers a `mosh-client.exe` sitting next to it,
  so this will be a drop-in swap with no configuration change.
- `mosh-server.exe`, hosting PowerShell through the Windows Pseudo Console. This
  is the part nobody has done. See
  [docs/windows-port-notes.md](docs/windows-port-notes.md) for what has been
  measured so far.

**Build it yourself.**

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build
```

The CMake build also works on Linux, which is how it is validated without a
Windows machine. The autotools build is untouched and remains the way to build
on Unix.

---

Why a fork
----------

Upstream has declined this, in writing, twice:

- [PR #1269](https://github.com/mobile-shell/mosh/pull/1269), an earlier and
  more conservative Windows patch, closed in 2023: *"I do not believe the mosh
  maintainers are currently interested in supporting a native win32 build. We
  recommend using mosh in WSL."*
- [PR #1322](https://github.com/mobile-shell/mosh/pull/1322), a two-line fix,
  closed 45 minutes after it was opened: *"We have no CI to ensure this
  continues to work, so we will not be merging this fix."* Upstream CI runs
  macOS and Ubuntu only, so that reasoning applies permanently to anything
  Windows-shaped.

This fork exists to do the work, not to argue with that. Upstream is treated as
read-only and rebased onto, and commits are kept POSIX-clean where possible so
individual fixes stay cherry-pickable if upstream ever wants them.

---

Alternatives
------------

If you only want a mosh **client** on Windows and do not care whose:

- [MoshCatty](https://github.com/binaricat/MoshCatty) — the client this project
  currently uses. Also usable directly.
- [Netcatty](https://github.com/binaricat/Netcatty) — a GUI SSH client with mosh
  built in.
- Chrome Secure Shell, Termius, and Blink on iOS all ship mosh clients.

None of them can act as a mosh **server** on Windows.

---

Credits and license
-------------------

Mosh is by Keith Winstein and the Mosh developers — see `AUTHORS` and `THANKS`.
This fork adds a Windows port on top of their work and claims nothing else.

GPL v3, as upstream, with upstream's OpenSSL linking exception preserved; see
`COPYING`. Changes are recorded in the git history. This is a modified version
of Mosh and is not endorsed by or affiliated with the upstream project.
