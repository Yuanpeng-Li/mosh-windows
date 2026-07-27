Native Windows port of Mosh
===========================

This is a fork of [mobile-shell/mosh](https://github.com/mobile-shell/mosh)
that is being ported to run natively on Windows: MSVC, CMake and vcpkg, with
`mosh-server` hosting a PowerShell session through the Windows Pseudo Console
(ConPTY). No Cygwin, no MinGW runtime, no WSL.

The interesting half is the **server**. A native Windows Mosh *client* already
exists — see [Prior art](#prior-art) — but as far as I can find, nothing lets
you `mosh` *into* a Windows machine and get a PowerShell session that survives
sleep, roaming and network loss. That is what this is for.

Status
------

**Not usable yet.** What exists today:

| | |
|---|---|
| CMake build, Linux | works; passes the full automake suite (30 PASS / 2 XFAIL / 0 FAIL) and ctest 4/4 |
| CMake build, Windows/MSVC | configures; the protobuf library builds and links |
| Everything else on Windows | not written |

The autotools build is untouched and still the way to build on Unix.

Why not upstream
----------------

These changes are not being sent to upstream Mosh, for reasons upstream has
stated plainly:

* [PR #1269](https://github.com/mobile-shell/mosh/pull/1269) — an earlier and
  more conservative Windows patch (MinGW, reusing the existing autotools
  build) was closed unmerged in 2023: *"I do not believe the mosh maintainers
  are currently interested in supporting a native win32 build. We recommend
  using mosh in WSL."*
* [PR #1322](https://github.com/mobile-shell/mosh/pull/1322) — a two-line fix
  was closed 45 minutes after it was opened: *"We have no CI to ensure this
  continues to work, so we will not be merging this fix."* Upstream CI runs
  macOS and Ubuntu only, so that objection applies permanently to anything
  Windows-shaped.

This fork exists to do the work, not to disagree with that decision. Upstream
is treated as read-only and rebased onto; commits are kept small and
POSIX-clean where possible so individual fixes stay cherry-pickable if upstream
ever wants them.

What has been established on real hardware
------------------------------------------

These were measured on Windows 11 (build 26200, MSVC 14.44), not taken from
documentation, and each one is a trap that costs days if you meet it late.

**ConPTY does not give the child its standard handles.** A process launched
with `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` *attaches* to the pseudoconsole —
`GetConsoleWindow()` is non-NULL and the pty's conhost sets its title from the
child image — but `CreateProcess` still populates the child's standard handles
from the parent's. If the parent's stdout is a pipe (which it is under sshd),
the child writes there and the pty stream contains only ConPTY's own
initialisation sequences. The fix is `STARTF_USESTDHANDLES` with all three
handles NULL. `CREATE_NO_WINDOW` breaks it again — the child then lands on a
different, default-sized console.

**Windows OpenSSH puts session processes in a kill-on-close job object.**
Measured `LimitFlags = 0x2800`: `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` set,
`JOB_OBJECT_LIMIT_BREAKAWAY_OK` set. A detached child without breakaway died
about a second after ssh disconnected; with `CREATE_BREAKAWAY_FROM_JOB` it
survived. Since surviving disconnection is the entire point of Mosh, this is
not optional. Breakaway is permitted here but is not contractual, so the code
has to handle `ERROR_ACCESS_DENIED` and say which path it took.

**`wchar_t` is 16 bits under MSVC, and Mosh stores one code point per
`wchar_t`.** `U+1F600` stored and read back yields `U+F600`. Worse than
mojibake: `wcrtomb` returns `(size_t)-1` for a lone surrogate, and
`terminalframebuffer.h` then does `contents.insert(contents.end(), tmp, tmp +
len)`. The first emoji can corrupt the heap. The internal character type has to
become `char32_t` before any of the terminal code is touched.

**POSIX errno constants do not match Winsock error codes.** In the MSVC CRT
`EWOULDBLOCK` is 140, while `WSAGetLastError()` returns `WSAEWOULDBLOCK` =
10035. `network.cc`'s `(e == EAGAIN || e == EWOULDBLOCK)` therefore compiles
cleanly and is always false. Windows also reports ICMP port-unreachable as
`WSAECONNRESET` on the next receive from an *unconnected* UDP socket, which
POSIX Mosh never sees; `SIO_UDP_CONNRESET` has to be turned off on every
socket, including the ones created by port hopping.

**Console resize events report the buffer, not the window.** In VT input mode
`WINDOW_BUFFER_SIZE_EVENT` still arrives through `ReadConsoleInputW`, so it can
stand in for `SIGWINCH` — but its payload was 120x9001 where the window was
120x30. The terminal size has to come from `GetConsoleScreenBufferInfo`'s
`srWindow`, not from the event.

Building
--------

Linux, to validate CMake changes without a Windows machine:

    cmake -S . -B build -G Ninja
    cmake --build build
    ctest --test-dir build

Windows, with Visual Studio Build Tools and a vcpkg checkout:

    cmake -S . -B build -G Ninja ^
      -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake ^
      -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
    cmake --build build

In-source builds are refused: they would write over the autotools Makefiles and
`src/include/config.h`.

Prior art
---------

For the **client** side on Windows, use one of these rather than waiting for
this fork:

* [MoshCatty](https://github.com/binaricat/MoshCatty) — a pure-Rust Mosh client,
  wire-compatible with stock `mosh-server`, shipping static-CRT Windows
  binaries. It is the engine behind the
  [Netcatty](https://github.com/binaricat/Netcatty) SSH client.
* Chrome Secure Shell, Termius, and Blink on iOS all ship Mosh clients.

None of them provide a Windows `mosh-server`.

Credits and license
-------------------

Mosh is by Keith Winstein and the Mosh developers; see `AUTHORS` and `THANKS`.
This fork adds a Windows port on top of their work and claims nothing else.

GPL v3, as upstream, with upstream's OpenSSL linking exception preserved. See
`COPYING`. Files modified here are marked as changed in the git history; this
file records that this tree is a modified version of Mosh and is not endorsed
by or affiliated with the upstream project.
