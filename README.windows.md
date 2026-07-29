mosh for Windows
================

`mosh user@host`, natively on Windows. No Cygwin, no WSL, no MSYS2.

[Mosh](https://mosh.org) is a remote terminal that survives sleep, roaming and
packet loss, and echoes your typing locally so a laggy link still feels
responsive. It has never had a native Windows build. This fork is one.

Both directions work:

| | |
|---|---|
| **Out** — `mosh user@host` from Windows to a Linux or macOS server | works |
| **In** — `mosh user@windows-box` from anywhere, and get a PowerShell session | works, and is the part nobody had done |

Built from mosh's own C++ source, so the terminal emulator, the state-synchronisation
protocol and the prediction engine are upstream's, not a reimplementation. It
interoperates with stock mosh 1.4.0 in both directions.

---

Install
-------

### Download it, read it, run it

```powershell
irm https://raw.githubusercontent.com/Yuanpeng-Li/mosh-windows/windows/scripts/install.ps1 -OutFile install.ps1
notepad install.ps1        # it is 180 lines; the interesting part is what it downloads
.\install.ps1
```

Installs `mosh.exe`, `mosh.ps1`, `mosh.cmd`, `mosh-client.exe` and
`mosh-server.exe` into `%LOCALAPPDATA%\Programs\mosh` and adds that one
directory to your **user** PATH. No admin, nothing written anywhere else.

### One line

```powershell
irm https://raw.githubusercontent.com/Yuanpeng-Li/mosh-windows/windows/scripts/install.ps1 | iex
```

Same thing, in one go. Worth knowing before you use it: fetching a script and
piping it straight into `iex` is also how a good deal of malware arrives, and
Defender's behavioural engine scores it accordingly — running it wrapped in
`powershell -EncodedCommand`, as an automation harness might, was enough to
raise `Trojan:Win32/Commando.A!ml` on the machine this port was developed on.
Nothing was detected in any file; the command line alone did it. The three-step
form above avoids that, and lets you read what you are about to run.

Then open a **new** terminal:

```powershell
mosh user@host
```

### winget

```powershell
winget install YuanpengLi.MoshWindows
```

### By hand

Unpack the
[release archive](https://github.com/Yuanpeng-Li/mosh-windows/releases/latest)
into a directory on your PATH.

`mosh.exe` is a small shim that finds `mosh.ps1` beside it and runs it under
PowerShell; it exists because a `.ps1` cannot be put on PATH and invoked as a
command, and because PowerShell's `-File` splits an argument like
`--client=C:\path` at the colon. From a source checkout without a compiler,
`mosh.cmd` does the same job, with the caveat that batch cannot quote an
argument containing a space for PowerShell's parser.

### Check it works

```powershell
mosh --local localhost
```

That starts a server and a client on this machine, with no ssh in between, and
drops you in a PowerShell session. If that works, the binaries are fine and
anything else is network or ssh.

### Uninstall

```powershell
mosh-uninstall
```

---

Connecting out
--------------

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
```

One difference from upstream, reported by `--help`:
`--experimental-remote-ip=proxy` is refused. It works by re-invoking the
launcher as an ssh `ProxyCommand`, and Win32 OpenSSH runs `ProxyCommand`
through `cmd.exe`, whose quoting is not `sh`'s. The default here is `remote`,
which asks the server for its address via `$SSH_CONNECTION`.

The far end needs mosh from its distribution — nothing from this project:

```sh
apt install mosh        # Debian, Ubuntu
dnf install mosh        # Fedora, RHEL
brew install mosh       # macOS
```

and inbound **UDP 60000–61000** open. Most VPS firewalls block it by default:

```sh
sudo ufw allow 60000:61000/udp
```

---

Connecting in
-------------

To reach this machine with `mosh user@windows-box`, three things have to be
true on the Windows side.

**1. mosh installed, and `mosh-server` on PATH under that name.** The installer
does this. It matters that it is on PATH rather than behind a shortcut: the
client asks ssh to run `mosh-server`, and a session started by sshd sees only
PATH.

**2. OpenSSH Server running.** Windows ships it as an optional feature:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
Start-Service sshd
Set-Service sshd -StartupType Automatic
```

**3. A firewall rule for `mosh-server.exe`.** Once, in an elevated PowerShell:

```powershell
New-NetFirewallRule -DisplayName 'mosh-server (UDP)' `
  -Direction Inbound -Action Allow -Protocol UDP `
  -Program "$env:LOCALAPPDATA\Programs\mosh\mosh-server.exe" -Profile Any
```

Per-program rather than per-port on purpose: mosh picks a port in 60000–61000
and may change it while roaming, so a rule pinned to one port breaks the moment
it does. Undo with
`Remove-NetFirewallRule -DisplayName 'mosh-server (UDP)'`.

Then, from any machine with mosh:

```sh
mosh user@192.168.1.10
```

You get PowerShell 7 if `pwsh.exe` is installed, Windows PowerShell otherwise,
and `%ComSpec%` failing both. Override it the usual way:

```sh
mosh user@192.168.1.10 -- cmd.exe
```

The session survives the ssh connection closing, sleep, and changing networks,
which is the entire point.

### Your Linux or macOS client needs one thing

The protocol is unchanged, so a stock `mosh-client` works. The **launcher** does
not, and it is the one piece you have to update.

Stock `mosh` runs `ssh -n -tt`, and asking for a pty makes Windows sshd run the
command under a pseudoconsole. A ConPTY paints a screen instead of forwarding
bytes, so the `MOSH CONNECT` line either arrives glued to escape sequences —
where mosh's start-of-line match cannot see it — or the command does not run at
all. Neither is fixable from the Windows side; `docs/windows-port-notes.md`
records what was tried.

The launcher is a Perl script, so replacing it costs one command and leaves
your packaged `mosh-client` alone:

```sh
curl -fsSL https://raw.githubusercontent.com/Yuanpeng-Li/mosh-windows/windows/scripts/install-launcher.sh | sh
```

Then `mosh user@windows-box` works with no flags. `rm $(command -v mosh)` undoes
it. Without it, pass `--no-ssh-pty` every time:

```sh
mosh --no-ssh-pty user@192.168.1.10
```

The symptom to recognise, if you skip this: mosh prints a perfectly clean
`MOSH CONNECT 60001 …` line and then says it did not find the server startup
message. The escapes wrapping that line are invisible on a terminal.

---

Troubleshooting
---------------

**`Did not find mosh server startup message`**
`mosh-server` is not on the remote PATH, or the remote locale is not UTF-8, or
something in the remote shell's startup files printed first. Check with
`ssh user@host mosh-server --version`, and try `--locale=en_US.UTF-8`. Against a
Windows server from a stock client, add `--no-ssh-pty` (see above).

**`Nothing received from server on UDP port 60001`**
ssh worked and the server started, but its UDP replies are not arriving. Almost
always a firewall between you and the host. Note the port *range*, not just one
port — mosh hops ports while roaming. On a Windows server, check the rule is
per-program and points at the `mosh-server.exe` that is actually running.

**`mosh: Cannot find mosh-client`**
Point `--client` at your build, or set `MOSH_CLIENT`. `mosh --setup` will also
download one.

**`this console cannot interpret terminal sequences`**
The client needs a console that understands VT sequences, which means Windows
10 1809 or newer. Windows Terminal, the modern console host and VS Code's
terminal all qualify.

**Chinese, Japanese or emoji are misaligned**
Client and server must agree on how wide each character is. This port carries
its own width table, generated from glibc's `wcwidth`, precisely so that a
Windows client and a Linux server agree. If you still see a shift, please
[open an issue](https://github.com/Yuanpeng-Li/mosh-windows/issues) with the
exact text.

**The prompt redraws oddly, or local echo never engages**
Try `mosh --predict=never user@host`. If that fixes it, the prediction engine
is disagreeing with your shell's redraw behaviour; an issue with your shell and
prompt would be useful.

**Ctrl-Z does nothing**
Correct. Windows has no job control, so there is nothing to suspend to.

---

Build it yourself
-----------------

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build
```

Needs MSVC (VS Build Tools 2022), CMake, Ninja and vcpkg; `vcpkg.json` pins the
dependencies. The same CMake build works on Linux and macOS, which is how the
port is kept honest — every change is built and tested on both, and
`ctest` there runs the whole `make check` suite, tmux screen captures included.
The autotools build is untouched and remains the way to build on Unix.
`docs/cmake-build.md` lists the options and what each corresponds to in
`configure`.

`docs/windows-port-notes.md` records the Windows behaviours this port had to be
built around, each one measured rather than taken from documentation.

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

- [MoshCatty](https://github.com/binaricat/MoshCatty) — an independent Rust
  reimplementation of the mosh protocol.
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
