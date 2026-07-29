#!/bin/sh
#
# Install this fork's `mosh` launcher on Linux or macOS, so that
# `mosh user@windows-box` works against a Windows mosh-server with no flags.
#
#   curl -fsSL https://raw.githubusercontent.com/Yuanpeng-Li/mosh-windows/windows/scripts/install-launcher.sh | sh
#
# Why this exists, in one paragraph. Stock mosh runs `ssh -n -tt`, and Windows
# OpenSSH runs a remote command under a pseudoconsole whenever a pty is asked
# for. A ConPTY paints a screen rather than streaming bytes, so the handshake
# line arrives glued to escape sequences and mosh.pl's anchored match never
# fires -- or, when -n leaves ssh unable to read a window size, the 0x0
# pseudoconsole never starts the command at all. Neither is fixable from the
# server: see docs/windows-port-notes.md for the measurements. What IS fixable
# is the launcher, which is a Perl script and nothing more. This fork's copy
# notices the failure and retries once without the pty.
#
# Only the launcher is replaced. mosh-client stays whatever your package
# manager installed -- this does not build or download a binary.

set -eu

REPO_RAW=${MOSH_LAUNCHER_URL:-https://raw.githubusercontent.com/Yuanpeng-Li/mosh-windows/windows/scripts/mosh.pl}
VERSION=${MOSH_LAUNCHER_VERSION:-1.4.0-windows-fork}

say()  { printf '%s\n' "$*"; }
step() { printf '==> %s\n' "$*"; }
warn() { printf '!!  %s\n' "$*" >&2; }
die()  { printf '!!  %s\n' "$*" >&2; exit 1; }

# ------------------------------------------------------------- prerequisites --

command -v perl >/dev/null 2>&1 || die "perl is required (the launcher is a Perl script)."

if ! command -v mosh-client >/dev/null 2>&1; then
    warn "mosh-client is not on your PATH."
    warn "This installs the launcher only. Install mosh itself first:"
    warn "    brew install mosh        # macOS"
    warn "    sudo apt install mosh    # Debian, Ubuntu"
    warn "    sudo dnf install mosh    # Fedora, RHEL"
    die  "Nothing was changed."
fi

# ------------------------------------------------------------------ where to --
# Prefer a directory that already exists and comes BEFORE the packaged mosh on
# PATH; installing somewhere later would be a no-op the user could not see.

existing=$(command -v mosh 2>/dev/null || true)

pick_dir() {
    if [ -n "${PREFIX:-}" ]; then
        printf '%s\n' "${PREFIX%/}/bin"
        return
    fi
    for d in /usr/local/bin "$HOME/.local/bin" "$HOME/bin"; do
        case ":$PATH:" in
            *":$d:"*) printf '%s\n' "$d"; return;;
        esac
    done
    printf '%s\n' "$HOME/.local/bin"
}

DIR=$(pick_dir)
TARGET="$DIR/mosh"

# Would something else still win? Compare positions in PATH.
if [ -n "$existing" ] && [ "$existing" != "$TARGET" ]; then
    other_dir=$(dirname "$existing")
    winner=""
    IFS=:
    for p in $PATH; do
        [ -n "$p" ] || continue
        if [ "$p" = "$DIR" ];       then winner="new";   break; fi
        if [ "$p" = "$other_dir" ]; then winner="other"; break; fi
    done
    unset IFS
    if [ "$winner" = "other" ]; then
        warn "$existing comes before $DIR on your PATH and would keep winning."
        warn "Re-run with PREFIX set to somewhere earlier, e.g."
        warn "    curl -fsSL <url> | PREFIX=\$HOME/.local sh"
        warn "or move $DIR earlier in PATH."
        die  "Nothing was changed."
    fi
fi

# --------------------------------------------------------------------- fetch --

step "Fetching the launcher"
tmp=$(mktemp "${TMPDIR:-/tmp}/mosh-launcher.XXXXXX")
trap 'rm -f "$tmp"' EXIT INT TERM

if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$REPO_RAW" -o "$tmp"
elif command -v wget >/dev/null 2>&1; then
    wget -qO "$tmp" "$REPO_RAW"
else
    die "neither curl nor wget is available."
fi

# The checked-in script carries the placeholders the build normally expands.
sed -e "s/@VERSION@/$VERSION/g" -e "s/@PACKAGE_STRING@/mosh 1.4.0/g" "$tmp" > "$tmp.out"
mv "$tmp.out" "$tmp"

# Refuse to install something that does not parse, rather than replacing a
# working `mosh` with a broken one.
perl -c "$tmp" >/dev/null 2>&1 || die "the downloaded launcher does not parse; nothing was changed."
grep -q 'MOSH_NO_PTY_RETRY' "$tmp" \
    || die "the downloaded launcher has no --no-ssh-pty retry; it is not this fork's copy."

# ------------------------------------------------------------------- install --

step "Installing to $TARGET"
mkdir -p "$DIR"

# Remove first, always. On an Intel Mac /usr/local/bin/mosh is a Homebrew
# symlink into the Cellar, and cp follows it -- so copying "to /usr/local/bin"
# silently rewrites the file inside the formula, and the documented undo
# (rm $(command -v mosh)) then deletes the link and leaves the formula broken.
# Removing the link and creating a plain file leaves brew's own copy intact and
# recoverable with `brew link --overwrite mosh`.
if [ -L "$TARGET" ] || [ -e "$TARGET" ]; then
    if [ -w "$DIR" ]; then
        rm -f "$TARGET"
    elif command -v sudo >/dev/null 2>&1; then
        sudo rm -f "$TARGET"
    fi
fi

if [ -w "$DIR" ]; then
    cp "$tmp" "$TARGET" && chmod 755 "$TARGET"
elif command -v sudo >/dev/null 2>&1; then
    say "  $DIR needs root; using sudo"
    sudo cp "$tmp" "$TARGET" && sudo chmod 755 "$TARGET"
else
    die "$DIR is not writable and sudo is not available. Re-run with PREFIX=\$HOME/.local"
fi

# --------------------------------------------------------------------- check --

hash -r 2>/dev/null || true
resolved=$(command -v mosh 2>/dev/null || true)
say ""
step "Installed"
say "  $("$TARGET" --version 2>&1 | head -n 1)"
if [ "$resolved" != "$TARGET" ]; then
    warn "but \`mosh\` still resolves to $resolved in this shell."
    warn "Open a new shell, or put $DIR earlier in PATH."
else
    say "  mosh -> $TARGET"
fi
say ""
say "  mosh user@windows-box     now works with no extra flags"
say "  rm $TARGET                undoes this"
