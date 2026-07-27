<#
.SYNOPSIS
    mosh -- the mobile shell. Windows launcher.

.DESCRIPTION
    The Windows counterpart of scripts/mosh.pl. It does what the Perl script
    does: ssh to the host, start mosh-server there, read back the port and
    session key it prints, and hand those to a local mosh-client over UDP.

    This is deliberately independent of the C++ port -- it drives whatever
    mosh-client.exe it can find, so it works today with an existing Windows
    client build and will keep working with this tree's own once that exists.

.PARAMETER Destination
    [user@]host, as you would give ssh.

.EXAMPLE
    mosh lyp@10.0.0.5
    mosh -SshPort 2022 lyp@example.com
    mosh server -- tmux attach
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Destination,

    # Port for the ssh bootstrap connection (ssh -p).
    [int] $SshPort,

    # UDP port or range for mosh-server to bind (mosh-server -p).
    [string] $Port,

    # Locale to hand mosh-server. Windows has no LANG, and mosh-server refuses
    # to run outside a UTF-8 locale, so one has to be synthesised.
    [string] $Locale,

    [int] $Colors = 256,

    # Path to mosh-client.exe, if it is not next to this script or on PATH.
    [string] $Client,

    # Remote mosh-server command.
    [string] $Server = 'mosh-server',

    # Extra options passed straight to ssh, e.g. -SshOption '-i','~/.ssh/id_ed25519'
    [string[]] $SshOption = @(),

    # Anything after -- is the command to run on the remote host.
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Command = @()
)

# Deliberately NOT 'Stop'. This script drives native programs whose stderr
# PowerShell converts into error records; mosh-server writes its banner there,
# and under 'Stop' that ordinary line becomes a terminating error.
$ErrorActionPreference = 'Continue'

function Die($msg) {
    Write-Host "mosh: $msg" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------- client ----

function Find-MoshClient {
    if ($Client) {
        if (Test-Path -LiteralPath $Client) { return (Resolve-Path -LiteralPath $Client).Path }
        Die "mosh-client not found at: $Client"
    }
    if ($env:MOSH_CLIENT -and (Test-Path -LiteralPath $env:MOSH_CLIENT)) {
        return (Resolve-Path -LiteralPath $env:MOSH_CLIENT).Path
    }
    $here = Split-Path -Parent $PSCommandPath
    foreach ($n in 'mosh-client.exe', 'mosh-client') {
        $p = Join-Path $here $n
        if (Test-Path -LiteralPath $p) { return (Resolve-Path -LiteralPath $p).Path }
    }
    $cmd = Get-Command mosh-client -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    Die @"
no mosh-client found.

Looked at: -Client, `$env:MOSH_CLIENT, next to this script, and PATH.
Put a mosh-client.exe beside this script, or set MOSH_CLIENT.
"@
}

$clientPath = Find-MoshClient

# ------------------------------------------------------------------ ssh ----

$sshExe = (Get-Command ssh -ErrorAction SilentlyContinue)
if (-not $sshExe) { Die "ssh not found on PATH (install the Windows OpenSSH client)" }

# Pick the locale to request. Unix mosh forwards the caller's LANG/LC_*; there
# is no such thing here, so fall back to a UTF-8 locale that is present on
# essentially every current Linux. Override with -Locale for hosts that only
# have, say, en_US.UTF-8.
if (-not $Locale) {
    if ($env:MOSH_LOCALE) { $Locale = $env:MOSH_LOCALE }
    elseif ($env:LANG)    { $Locale = $env:LANG }
    else                  { $Locale = 'C.UTF-8' }
}

# mosh-server's own argument list, in the order scripts/mosh.pl builds it.
$serverArgs = @('new', '-c', "$Colors", '-s')
if ($Port) { $serverArgs += @('-p', $Port) }
$serverArgs += @('-l', "LANG=$Locale", '-l', "LC_ALL=$Locale")
if ($Command.Count -gt 0) { $serverArgs += @('--') + $Command }

# The remote side runs this through a POSIX shell, so quote for sh -- not for
# PowerShell and not for cmd.
function ConvertTo-ShQuoted([string[]] $words) {
    ($words | ForEach-Object {
        if ($_ -match '^[A-Za-z0-9_./:@%+=-]+$') { $_ }
        else { "'" + ($_ -replace "'", "'\''") + "'" }
    }) -join ' '
}

# Ask the remote end which address ssh actually reached it on, and connect the
# UDP session to that. Without this the client is handed whatever string the
# user typed -- which for an ssh_config alias like "myserver" is not a name
# DNS can resolve, and on a multi-homed host is not necessarily the address
# mosh-server bound to. This is what mosh.pl's --experimental-remote-ip=remote
# does. The user's login shell may not be POSIX, hence the explicit sh -c.
$probe = '[ -n "$SSH_CONNECTION" ] && printf "\nMOSH SSH_CONNECTION %s\n" "$SSH_CONNECTION"'
$remoteCmd = 'sh -c ' + (ConvertTo-ShQuoted @($probe)) + ' ; ' +
             "$Server " + (ConvertTo-ShQuoted $serverArgs)

$sshArgs = @()
if ($SshPort) { $sshArgs += @('-p', "$SshPort") }

# ssh reads stdin and forwards it, which is what makes password and 2FA
# prompts work -- so normally leave it alone. But when there is no console
# (a scheduled task, a detached process, a CI step) stdin is not a usable
# handle and ssh blocks on it forever. Detect that and pass -n.
$hasConsole = $true
try { $null = [System.Console]::KeyAvailable } catch { $hasConsole = $false }
if (-not $hasConsole) { $sshArgs += '-n' }

$sshArgs += $SshOption
$sshArgs += @($Destination, '--', $remoteCmd)

# ------------------------------------------------------- run the bootstrap --
# stdout is captured so we can read MOSH CONNECT; stdin and stderr stay on the
# console so password and 2FA prompts still work.

# The redirection is done by cmd, not by PowerShell. PowerShell turns a native
# program's stderr into error records, and mosh-server writes its banner there,
# which under $ErrorActionPreference = 'Stop' aborts the script -- or worse,
# wedges it. Letting cmd do it keeps both streams as plain file handles: stdout
# to the temp file for us to parse, stderr and stdin straight through to the
# console so password and 2FA prompts still work.
function ConvertTo-CmdQuoted([string] $s) { '"' + ($s -replace '"', '\"') + '"' }

$outFile = [System.IO.Path]::GetTempFileName()
try {
    $cmdLine = (@($sshExe.Source) + $sshArgs | ForEach-Object { ConvertTo-CmdQuoted $_ }) -join ' '
    $cmdLine += ' > ' + (ConvertTo-CmdQuoted $outFile)
    & cmd.exe /c $cmdLine
    $sshExit = $LASTEXITCODE
    $lines = @(Get-Content -LiteralPath $outFile -ErrorAction SilentlyContinue)
} finally {
    Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue
}

$ip = $null; $port = $null; $key = $null
foreach ($line in $lines) {
    $l = $line.TrimEnd()
    if ($l -match '^MOSH IP (\S+)\s*$') { $ip = $Matches[1]; continue }
    # SSH_CONNECTION is "<client ip> <client port> <server ip> <server port>",
    # so with the two leading words the server address is field 4.
    if ($l -match '^MOSH SSH_CONNECTION ') {
        $w = $l -split '\s+'
        if ($w.Count -eq 6) { $ip = $w[4] }
        continue
    }
    if ($l -match '^MOSH CONNECT (\d+) ([A-Za-z0-9/+]{22})\s*$') {
        $port = $Matches[1]; $key = $Matches[2]; continue
    }
    # mosh-server's own diagnostics are worth showing; its banner is not.
    if ($l -and $l -notmatch '^mosh-server \(' -and $l -notmatch '^Copyright ' -and
        $l -notmatch '^License ' -and $l -notmatch '^This is free software') {
        Write-Host $l
    }
}

if (-not $key) {
    if ($sshExit -ne 0) { Die "ssh exited with status $sshExit; did not get a session from mosh-server." }
    Die @"
did not find a 'MOSH CONNECT' line in the server's output.

Usually one of:
  * mosh-server is not installed on the remote host, or not on its PATH
  * the remote locale is not UTF-8 -- try -Locale en_US.UTF-8
  * something in the remote shell's startup files printed before mosh-server ran
"@
}

# mosh-server was told -s, so it reports the address ssh came from. If it did
# not, fall back to the host we were given.
if (-not $ip) {
    $ip = $Destination -replace '^.*@', ''
}

# ------------------------------------------------------------- connect -----
# The key goes through the environment, never the command line: command lines
# are visible to every process on the machine and are logged by WMI, Sysmon and
# Defender. Invoke the client directly so it inherits this console.

$env:MOSH_KEY = $key
try {
    & $clientPath $ip $port
    $rc = $LASTEXITCODE
} finally {
    Remove-Item Env:\MOSH_KEY -ErrorAction SilentlyContinue
}
exit $rc
