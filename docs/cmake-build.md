The CMake build
===============

This tree has two build systems. The autotools one is upstream's and is
untouched; the CMake one exists because autotools cannot produce a native MSVC
binary, and the Windows port needs one.

They are peers, not alternatives-in-name-only: the same CMake build is used on
Linux and macOS, and every change is built and tested under both. That is the
only thing keeping the Windows port from quietly diverging.

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Options
-------

Each one is the CMake spelling of a `configure` flag, and defaults to the same
answer `configure` would give.

| CMake | configure | default |
|---|---|---|
| `-DMOSH_CRYPTO_LIBRARY=<lib>` | `--with-crypto-library=<lib>` | `apple-common-crypto` where CommonCrypto exists, else `openssl` |
| `-DMOSH_BUILD_CLIENT=OFF` | `--disable-client` | on |
| `-DMOSH_BUILD_SERVER=OFF` | `--disable-server` | on |
| `-DMOSH_BUILD_EXAMPLES=ON` | `--enable-examples` | off |
| `-DMOSH_ENABLE_FUZZING=ON` | `--enable-fuzzing` | off |
| `-DMOSH_ENABLE_SYSLOG=ON` | `--enable-syslog` | off |
| `-DMOSH_HARDEN=OFF` | `--disable-hardening` | on |
| `-DBUILD_TESTING=OFF` | — | on |

`MOSH_CRYPTO_LIBRARY` takes `openssl` (mosh's own OCB over OpenSSL's AES, which
is what upstream ships), `openssl-with-openssl-ocb`, `nettle`, or
`apple-common-crypto`. The configure summary line has an equivalent here:

```
--   cryptography : internal OCB, Nettle AES
```

`MOSH_ENABLE_FUZZING` needs a compiler that accepts `-fsanitize=fuzzer`, i.e.
clang; the check runs at configure time and fails loudly rather than producing
`terminal_fuzzer` binaries that cannot link.

One deliberate difference from CMake's own defaults: optimised configurations
normally add `-DNDEBUG`, which would disarm the `assert()`s guarding the memcpy
into the fixed-size AEAD buffers in `src/crypto/crypto.cc`. The autotools build
never defines it, so `MOSH_KEEP_ASSERTS` (on by default) strips it back out and
the two build systems ship the same checks.

What ctest covers
-----------------

Everything `make check` runs, which is the point — 34 tests:

- Seven C++ unit tests (OCB, encrypt/decrypt, base64, nonce increment, UTF-8,
  character widths, and the Unicode round trip). These are pure computation and
  run on Windows too; the round-trip test is the one that catches a 16-bit
  `wchar_t`.
- `local.test` and the 26 display tests: `/bin/sh` driving `mosh` end to end
  inside tmux and comparing screen captures. POSIX only, and skipped (not
  failed) when tmux or a UTF-8 locale is missing — ctest reports those as
  `Skipped` via the scripts' own exit code 77.

`e2e-failure` and `emulation-attributes-256color8` are expected failures, as
they are in `XFAIL_TESTS`.

The scripted tests are the automake suite unmodified, run against the CMake
tree. That works because the two trees have the same shape:
`../frontend/mosh-{client,server}` and `../../scripts/mosh` relative to the
tests directory. One thing had to be fixed in `src/tests/e2e-test` to make it
possible: it built the path it hands to the in-tmux shell as
`"${PWD}/${test_script}"`, which assumes the harness invoked it by a bare
filename. Automake does; ctest passes an absolute path, and the two
concatenated into `/build/src/tests//src/tests/foo.test`. It now resolves the
script to an absolute path once, up front, which is also what a VPATH automake
build needed.

Out-of-source autotools builds
------------------------------

They do not work, and never did: `AM_CXXFLAGS = -I$(top_srcdir)/` with no
`top_builddir`, so `src/include/config.h` is not found. Build autotools
in-source. CMake is out-of-source as usual.
