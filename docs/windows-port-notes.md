Windows port: what has been measured
====================================

Working notes for the native Windows port. Everything here was measured on real
hardware — Windows 11 build 26200, MSVC 14.44, Windows SDK 10.0.26100 — rather
than taken from documentation, and each item is a trap that costs days if you
meet it late.

For using mosh on Windows, see [../README.windows.md](../README.windows.md).

---

ConPTY does not give the child its standard handles
---------------------------------------------------

A process launched with `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` *does* attach to
the pseudoconsole: `GetConsoleWindow()` is non-NULL in the child, and the pty's
conhost sets its window title from the child's image path. But `CreateProcess`
still fills the child's standard handles from the parent's process parameters.
When the parent's stdout is a pipe — which it is under sshd — the child writes
there, and the pty stream contains nothing but ConPTY's own initialisation
sequences.

Measured in the child:

```
GetConsoleWindow()   = 0x1500D8          <- attached to the pty
STD_OUTPUT handle    = 0x0C  GetFileType=3   <- FILE_TYPE_PIPE, not a console
GetConsoleMode(out)  = 0                  <- fails
WriteConsoleA        = 0, err=1           <- fails
WriteFile(stdout)    = 1, wrote=17        <- went to the parent's pipe
```

`FreeConsole()` in the parent does **not** fix it. What does:

| variant | result |
|---|---|
| baseline | child stdout `type=3` (pipe), `isConsole=0` |
| **`STARTF_USESTDHANDLES`, all three handles NULL** | **child stdout `type=2`, `isConsole=1`, `csbi size=80x25` — the pty's size** |
| `STARTF_USESTDHANDLES`, all three `INVALID_HANDLE_VALUE` | same, works |
| parent's own std handles set to NULL around the spawn | same, works |
| any of the above **plus `CREATE_NO_WINDOW`** | **breaks** — child gets a different 120x9001 console, pty receives 0 bytes |

Microsoft's guidance not to combine `STARTF_USESTDHANDLES` with a
pseudoconsole is aimed at the opposite mistake: passing *real* handles, which
does override the console. Passing NULL is the documented "no handles supplied"
case, and console initialisation then substitutes the attached console's
handles.

**Invariants for the pty child: `STARTF_USESTDHANDLES` with NULL handles; never
`CREATE_NO_WINDOW`, `DETACHED_PROCESS` or `CREATE_NEW_CONSOLE`.** Someone will
eventually add `CREATE_NO_WINDOW` to "hide the console" and silently break it.

ConPTY also opens with `\e[?9001h` (win32-input-mode) and `\e[?1004h` (focus
events), and repaints the whole screen on startup and on resize. mosh's parser
has to ignore both private modes gracefully.

---

Windows OpenSSH runs session processes in a kill-on-close job object
---------------------------------------------------------------------

Measured inside an sshd session:

```
in a job object     : YES
LimitFlags          : 0x00002800
  KILL_ON_JOB_CLOSE : SET      <- children die when ssh disconnects
  BREAKAWAY_OK      : SET      <- but breakaway is permitted
```

A detached child without breakaway died about a second after ssh disconnected.
With `CREATE_BREAKAWAY_FROM_JOB` it kept running — 45+ seconds of heartbeats
after disconnect, process still alive.

Surviving disconnection is the entire point of mosh, so this is not optional.
`BREAKAWAY_OK` is a property of the job sshd happens to create and is not
contractual, so the code must handle `CreateProcessW` failing with
`ERROR_ACCESS_DENIED`, retry without the flag, and record which path it took.
Jobs can also nest since Windows 8; breakaway escapes only the immediate one.

Breakaway does **not** lose the token, logon session, user profile or network
drive mappings — those belong to the token, not the job. What is lost is the
job's accounting and an administrator's ability to clean up by killing the job.
That is the same trade Unix daemonisation makes.

Separately: if the daemon inherits even one copy of sshd's stdout pipe, ssh
never finishes disconnecting and the launcher hangs after printing
`MOSH CONNECT`.

---

`wchar_t` is 16 bits under MSVC, and mosh stores one code point per `wchar_t`
------------------------------------------------------------------------------

```
sizeof(wchar_t)  = 2
U+1F600 stored in a wchar_t reads back as U+F600   (truncated)
```

`src/terminal/parseraction.h:48` is `wchar_t ch;` — one Unicode code point.
Same in `terminalframebuffer.h:177,182,197,411`.

The symptom is worse than mojibake. `terminalframebuffer.h:193`:

```cpp
size_t len = wcrtomb( tmp, c, &ps );
contents.insert( contents.end(), tmp, tmp + len );
```

`wcrtomb` returns `(size_t)-1` on failure, which is exactly what a lone
surrogate produces — and a lone surrogate is exactly what a 16-bit `wchar_t`
holding an astral code point *is*. `tmp + (size_t)-1` is a wild pointer. The
first emoji can corrupt the heap.

So the internal character type has to become `char32_t` before any terminal
code is touched. Two traps in that migration:

- `parseraction.h:57` is `Action() : ch( -1 )`. `wchar_t` is signed on Linux, so
  `ch == -1` today; as `char32_t` it becomes `0xFFFFFFFF` and
  `terminaldispatcher.cc:223`'s `assert( act->ch <= 255 )` starts firing. The
  sentinel has to change with the type.
- `std::wstring` is a second 16-bit problem. `terminaloverlay.cc:254-260`
  iterates one **UTF-16 code unit** at a time and asks `wcwidth` for each,
  which on Windows means asking for the width of a surrogate.

A Linux-only test proves nothing here: `wchar_t` is already 32 bits there, so a
`char32_t` build is behaviourally identical. The real gate is a
platform-independent codec round-trip test — U+1F600, U+20000, a lone
surrogate, an overlong encoding, a truncated sequence — run again under MSVC.

Client and server must also compute *identical* character widths, or everything
after the first divergent wide character is misplaced, silently. A Windows
client with a vendored width table talking to a glibc server is a real mixed
-version hazard, and the protocol carries no way to negotiate it.

---

POSIX errno constants do not match Winsock error codes
-------------------------------------------------------

```
EAGAIN         = 11      EWOULDBLOCK  = 140     (MSVC CRT)
WSAEWOULDBLOCK = 10035   WSAECONNRESET = 10054  (what WSAGetLastError returns)
```

`src/network/network.cc:410` is `(e.the_errno == EAGAIN || e.the_errno ==
EWOULDBLOCK)`. Under MSVC that compiles cleanly and is **always false**.

The reverse case exists too: `src/util/select.h:168` reads `errno` after
`select`, but Winsock reports through `WSAGetLastError()`. It would read a
stale CRT errno — one that `terminaldispatcher.cc` sets to 0 on every CSI
parameter parse.

Windows also reports ICMP port-unreachable as `WSAECONNRESET` on the next
receive from an **unconnected** UDP socket, a case POSIX mosh never sees since
it never calls `connect()`. Start the client before the server, and every
wakeup throws. `SIO_UDP_CONNRESET` has to be disabled on every socket,
including the ones port hopping creates.

Two more: `MSG_DONTWAIT` does not exist on Windows and `network.cc:56-58`
falls back to `MSG_NONBLOCK`, which does not exist either — the flag must
become 0 with `FIONBIO` set at creation. And `IPV6_V6ONLY` defaults to **1** on
Windows against 0 on Linux, so dual-stack needs it set explicitly.

Verified available and working: `WSARecvMsg` via `WSAIoctl`,
`SIO_UDP_CONNRESET`, `IP_ECN`, `IP_RECVECN`, `IP_MTU_DISCOVER`,
`WSAEventSelect` + `WaitForMultipleObjects`. `IP_TOS` is accepted but Windows
has ignored user-set TOS since Windows 2000 absent a registry override, so DSCP
marking is effectively a no-op.

---

Console resize events report the buffer, not the window
---------------------------------------------------------

In VT input mode `WINDOW_BUFFER_SIZE_EVENT` still arrives through
`ReadConsoleInputW`, so it works as the `SIGWINCH` substitute. But its payload
is the **screen buffer** size, not the window:

```
buffer  = 120x9001    <- what the event reports
window  = 120x30      <- what mosh must report as the terminal size
```

The size has to come from `GetConsoleScreenBufferInfo`'s `srWindow`, not from
the event.

Also confirmed on a real console: `ENABLE_VIRTUAL_TERMINAL_PROCESSING` and
`DISABLE_NEWLINE_AUTO_RETURN` both settable; `ENABLE_VIRTUAL_TERMINAL_INPUT`
and `ENABLE_WINDOW_INPUT` both settable; `ENABLE_PROCESSED_INPUT` clearable, so
Ctrl-C arrives as a keystroke to forward rather than a console signal. That
last point means `SetConsoleCtrlHandler` will never fire for Ctrl-C — it fires
only for close, logoff and shutdown, on a separate thread, with a hard timeout
after it returns.

Console state outlives the process, so a mosh-client that dies without
restoring the mode wedges the user's shell — worse than the termios equivalent.

---

Build notes
-----------

- **protobuf 6.x with proto2 and `LITE_RUNTIME` compiles and links under
  MSVC.** All three `.proto` files declare `optimize_for = LITE_RUNTIME`, so
  `protobuf::libprotobuf-lite` is enough and the descriptor, reflection and
  much of Abseil are avoided.
- **`src/crypto/ocb_internal.cc` needed two fixes to compile as C++ under
  MSVC**, both latent upstream defects rather than port-specific hacks: a
  `#define restrict __restrict` leaking into `<openssl/evp.h>`'s
  `__declspec(restrict)`, and `_BitScanForward` being passed an `unsigned*`
  where it takes `unsigned long*`.
- **clang-cl would not compile that file** even now: it defines `_MSC_VER`, and
  the unconditional `#define __SSSE3__` makes `_mm_shuffle_epi8` compile
  without the target feature clang requires. Not a problem for MSVC; recorded
  so it is not a surprise later.
- The system code page on the development machine is already UTF-8 (65001),
  which is convenient but must not be assumed.
