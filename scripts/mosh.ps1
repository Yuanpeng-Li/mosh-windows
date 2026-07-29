# mosh -- the mobile shell. Windows launcher.
#
# The counterpart of scripts/mosh.pl: ssh to the host, start mosh-server there,
# read back the port and session key it prints, and hand those to a local
# mosh-client over UDP.
#
# The command-line interface is deliberately the Perl script's, not
# PowerShell's. `mosh --predict=always -p 60000:60010 user@host` has to mean
# the same thing here as it does on Linux and macOS, so the arguments are
# parsed by hand rather than through [CmdletBinding()] -- which would rename
# every option, reorder them, and prompt interactively for a missing host
# instead of printing usage.
#
# Independent of the C++ port: it drives whatever mosh-client.exe it can find.

Set-StrictMode -Off
# Deliberately NOT 'Stop'. This script drives native programs whose stderr
# PowerShell converts into error records; mosh-server writes its banner there,
# and under 'Stop' that ordinary line becomes a terminating error.
$ErrorActionPreference = 'Continue'

$ProgName = 'mosh'

$Usage = @"
Usage: $ProgName [options] [--] [user@]host [command...]
        --client=PATH        mosh client on local machine
                                (default: "mosh-client")
        --server=COMMAND     mosh server on remote machine
                                (default: "mosh-server")

        --predict=adaptive      local echo for slower links [default]
-a      --predict=always        use local echo even on fast links
-n      --predict=never         never use local echo
        --predict=experimental  aggressively echo even when incorrect

-o      --predict-overwrite     prediction overwrites instead of inserting

-4      --family=inet        use IPv4 only
-6      --family=inet6       use IPv6 only
        --family=auto        autodetect network type for single-family hosts only
        --family=all         try all network types
        --family=prefer-inet use all network types, but try IPv4 first [default]
        --family=prefer-inet6 use all network types, but try IPv6 first
-p PORT[:PORT2]
        --port=PORT[:PORT2]  server-side UDP port or range
                                (No effect on server-side SSH port)
        --bind-server={ssh|any|IP}  ask the server to reply from an IP address
                                       (default: "ssh")

        --ssh=COMMAND        ssh command to run when setting up session
                                (example: "ssh -p 2222")
                                (default: "ssh")

        --no-ssh-pty         do not allocate a pseudo tty on ssh connection

        --no-init            do not send terminal initialization string

        --local              run mosh-server locally without using ssh

        --experimental-remote-ip=(local|remote)  select the method for
                             discovering the remote IP address to use for mosh
                             (default: "remote")

        --locale=LOCALE      UTF-8 locale to ask mosh-server for
                                (default: "C.UTF-8"; Windows has no LANG, and
                                 mosh-server requires a UTF-8 locale)

        --setup              download a mosh-client and install it beside this
                                script, then exit
        --help               this message
        --version            version and copyright information

Please report bugs to https://github.com/Yuanpeng-Li/mosh-windows/issues
Mosh home page: https://mosh.org
"@

$VersionMessage = @"
mosh 1.4.0 (Windows launcher)
Copyright 2012 Keith Winstein <mosh-devel@mit.edu>
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
"@

# The mosh-client --setup fetches. Pinned by digest: an unpinned download is
# an unreviewed binary from the internet, and this one terminates an encrypted
# session. Deliberately downloaded from its own project rather than
# redistributed here -- MoshCatty is somebody else's GPLv3 work, and shipping
# their binary would put the obligation to supply matching source on us.
$ClientRelease = @{
    Name    = 'MoshCatty'
    Version = 'moshcatty-0.1.8'
    Url     = 'https://github.com/binaricat/MoshCatty/releases/download/moshcatty-0.1.8/mosh-client-win32-x64.tar.gz'
    Sha256  = 'EE437592C351361E47C600FAFE04CE97A2551AA61FBEF1FB3D6DDA565835311E'
    Home    = 'https://github.com/binaricat/MoshCatty'
}

function Invoke-Setup([string] $destDir) {
    $exe = Join-Path $destDir 'mosh-client.exe'
    if (Test-Path -LiteralPath $exe) {
        Write-Output "mosh-client is already installed at $exe"
        Write-Output "Delete it first if you want to replace it."
        return 0
    }

    Write-Output "Fetching $($ClientRelease.Name) $($ClientRelease.Version)"
    Write-Output "  from $($ClientRelease.Url)"
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("mosh-client-" + [System.Guid]::NewGuid().ToString('N') + '.tar.gz')
    try {
        try {
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        } catch { }
        Invoke-WebRequest -Uri $ClientRelease.Url -OutFile $tmp -UseBasicParsing
    } catch {
        [Console]::Error.WriteLine("mosh: download failed: $_")
        return 1
    }

    $got = (Get-FileHash -LiteralPath $tmp -Algorithm SHA256).Hash
    if ($got -ne $ClientRelease.Sha256) {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        [Console]::Error.WriteLine("mosh: SHA-256 mismatch, refusing to install.")
        [Console]::Error.WriteLine("  expected $($ClientRelease.Sha256)")
        [Console]::Error.WriteLine("  got      $got")
        return 1
    }
    Write-Output "  SHA-256 verified"

    if (-not (Test-Path -LiteralPath $destDir)) {
        New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    }
    & tar.exe -xzf $tmp -C $destDir 2>&1 | Out-Null
    Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue

    if (-not (Test-Path -LiteralPath $exe)) {
        [Console]::Error.WriteLine("mosh: extraction did not produce $exe")
        return 1
    }
    Write-Output "Installed $exe"
    Write-Output ""
    Write-Output "$($ClientRelease.Name) is a separate GPLv3 project: $($ClientRelease.Home)"
    Write-Output "Try it:  mosh user@host"
    return 0
}

function Write-Usage-And-Exit([int] $code) {
    if ($code -eq 0) { Write-Output $Usage } else { [Console]::Error.WriteLine($Usage) }
    exit $code
}
function Die([string] $msg) {
    [Console]::Error.WriteLine("$ProgName`: $msg")
    exit 1
}
function Die-Usage([string] $msg) {
    [Console]::Error.WriteLine("$ProgName`: $msg`n")
    Write-Usage-And-Exit 1
}

# --------------------------------------------------------- argument parsing --

$client       = 'mosh-client'
$server       = 'mosh-server'
$predict      = $null
$overwrite    = $false
$bindIp       = $null
$useRemoteIp  = 'remote'
$family       = 'prefer-inet'
$portRequest  = $null
$sshWords     = @('ssh')
$termInit     = $true
$localhost    = $false
$locale       = $null
$userhost     = $null
$command      = @()

$argv = @($args)

# `pwsh -File script.ps1 --client=C:\path\to.exe` arrives split in two, as
# "--client=C" and "\path\to.exe": PowerShell's -File argument parser treats
# the colon as the -Name:Value separator. Invoking as `& script.ps1 @args`
# (what the profile function does) is unaffected, but the launcher should work
# either way. Rejoin the halves -- a lone drive letter followed by a token
# starting with a separator is never two real arguments.
$rejoined = @()
for ($j = 0; $j -lt $argv.Count; $j++) {
    $cur = [string]$argv[$j]
    if ($j + 1 -lt $argv.Count -and
        $cur -match '^--[^=]+=[A-Za-z]$' -and
        ([string]$argv[$j + 1]) -match '^[\\/]') {
        $rejoined += ($cur + ':' + [string]$argv[$j + 1])
        $j++
    } else {
        $rejoined += $cur
    }
}
$argv = $rejoined

$i = 0
$endOfOptions = $false

# Options that take a value may be written --opt=value or --opt value, as
# Getopt::Long accepts both.
function Next-Value([string] $name) {
    $script:i++
    if ($script:i -ge $script:argv.Count) { Die-Usage "option $name requires an argument" }
    return $script:argv[$script:i]
}

while ($i -lt $argv.Count) {
    $a = [string]$argv[$i]

    if ($endOfOptions -or $a -notmatch '^-' -or $a -eq '-') {
        $userhost = $a
        $i++
        break
    }

    if ($a -eq '--') { $endOfOptions = $true; $i++; continue }

    $name = $a; $val = $null; $hasVal = $false
    if ($a -match '^(--[^=]+)=(.*)$') { $name = $Matches[1]; $val = $Matches[2]; $hasVal = $true }

    switch -CaseSensitive ($name) {
        '--help'      { Write-Usage-And-Exit 0 }
        '--version'   { Write-Output $VersionMessage; exit 0 }
        '--setup'     { exit (Invoke-Setup (Split-Path -Parent $PSCommandPath)) }
        '--client'    { $client   = if ($hasVal) { $val } else { Next-Value $name } }
        '--server'    { $server   = if ($hasVal) { $val } else { Next-Value $name } }
        '--predict'   { $predict  = if ($hasVal) { $val } else { Next-Value $name } }
        '--port'      { $portRequest = if ($hasVal) { $val } else { Next-Value $name } }
        '-p'          { $portRequest = Next-Value $name }
        '--family'    { $family   = if ($hasVal) { $val } else { Next-Value $name } }
        '--bind-server' { $bindIp = if ($hasVal) { $val } else { Next-Value $name } }
        '--locale'    { $locale   = if ($hasVal) { $val } else { Next-Value $name } }
        '--ssh' {
            $s = if ($hasVal) { $val } else { Next-Value $name }
            # Getopt uses shellwords() here; the common cases are plain
            # whitespace splitting with optional quoting.
            $sshWords = [regex]::Matches($s, '"([^"]*)"|''([^'']*)''|(\S+)') | ForEach-Object {
                if ($_.Groups[1].Success) { $_.Groups[1].Value }
                elseif ($_.Groups[2].Success) { $_.Groups[2].Value }
                else { $_.Groups[3].Value }
            }
            if (-not $sshWords) { Die-Usage "--ssh needs a command" }
        }
        '--experimental-remote-ip' { $useRemoteIp = if ($hasVal) { $val } else { Next-Value $name } }
        '-a' { $predict = 'always' }
        '-n' { $predict = 'never' }
        '-4' { $family = 'inet' }
        '-6' { $family = 'inet6' }
        '-o' { $overwrite = $true }
        '--predict-overwrite'    { $overwrite = $true }
        '--no-predict-overwrite' { $overwrite = $false }
        '--init'        { $termInit = $true }
        '--no-init'     { $termInit = $false }
        '--ssh-pty'     { $sshPty = $true }
        '--no-ssh-pty'  { $sshPty = $false }
        '--local'       { $localhost = $true }
        default { Die-Usage "unrecognized option `"$a`"" }
    }
    $i++
}

# Everything after the host is the remote command.
if ($i -lt $argv.Count) { $command = @($argv[$i..($argv.Count - 1)]) }

if (-not $userhost) { Write-Usage-And-Exit 1 }

# --------------------------------------------------------------- validation --

if ($predict) {
    if ($predict -notin @('adaptive', 'always', 'never', 'experimental')) {
        Die-Usage "Unknown mode `"$predict`"."
    }
} elseif ($env:MOSH_PREDICTION_DISPLAY) {
    $predict = $env:MOSH_PREDICTION_DISPLAY
    if ($predict -notin @('adaptive', 'always', 'never', 'experimental')) {
        Die-Usage "Unknown mode `"$predict`" (MOSH_PREDICTION_DISPLAY in environment)."
    }
} else {
    $predict = 'adaptive'
}

if ($family -notin @('inet', 'inet6', 'auto', 'all', 'prefer-inet', 'prefer-inet6')) {
    Die-Usage "Unknown family `"$family`"."
}

if ($useRemoteIp -eq 'proxy') {
    Die @"
--experimental-remote-ip=proxy is not implemented on Windows.

It works by re-invoking this script as an ssh ProxyCommand, which Win32
OpenSSH runs through cmd.exe with different quoting rules. Use the default
(remote), which asks the server for its address via `$SSH_CONNECTION.
"@
}
if ($useRemoteIp -notin @('local', 'remote')) { Die-Usage "Unknown parameter $useRemoteIp" }



# ------------------------------------------------------- the local binaries --

# Only used by --local. Everywhere else the server is a name resolved on the
# far end, not a path here.
function Find-MoshServer([string] $want) {
    if ($want -ne 'mosh-server') {
        if (Test-Path -LiteralPath $want) { return (Resolve-Path -LiteralPath $want).Path }
        $c = Get-Command $want -ErrorAction SilentlyContinue
        if ($c) { return $c.Source }
        Die "Cannot find mosh-server at: $want"
    }
    $here = Split-Path -Parent $PSCommandPath
    foreach ($n in 'mosh-server.exe', 'mosh-server') {
        $p = Join-Path $here $n
        if (Test-Path -LiteralPath $p) { return (Resolve-Path -LiteralPath $p).Path }
    }
    $c = Get-Command mosh-server -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    Die "Cannot find mosh-server. Point --server at it, or put it on PATH."
}

function Find-MoshClient([string] $want) {
    if ($want -ne 'mosh-client') {
        if (Test-Path -LiteralPath $want) { return (Resolve-Path -LiteralPath $want).Path }
        $c = Get-Command $want -ErrorAction SilentlyContinue
        if ($c) { return $c.Source }
        Die "Cannot find mosh-client at: $want"
    }
    if ($env:MOSH_CLIENT -and (Test-Path -LiteralPath $env:MOSH_CLIENT)) {
        return (Resolve-Path -LiteralPath $env:MOSH_CLIENT).Path
    }
    $here = Split-Path -Parent $PSCommandPath
    foreach ($n in 'mosh-client.exe', 'mosh-client') {
        $p = Join-Path $here $n
        if (Test-Path -LiteralPath $p) { return (Resolve-Path -LiteralPath $p).Path }
    }
    $c = Get-Command mosh-client -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    Die @"
Cannot find mosh-client.

Run:  mosh --setup

That downloads one and verifies it against a pinned SHA-256. Or point --client
at your own, or set MOSH_CLIENT.

(Looked at: --client, `$env:MOSH_CLIENT, beside this script, and PATH.)
"@
}
$clientPath = Find-MoshClient $client

$sshExe = $null
if (-not $localhost) {
    $sshExe = Get-Command $sshWords[0] -ErrorAction SilentlyContinue
    if (-not $sshExe) { Die "Cannot find ssh: $($sshWords[0])" }
}

# --------------------------------------------------- build the remote command --

# Windows has no LANG, and mosh-server refuses to run outside a UTF-8 locale,
# so one has to be synthesised.
if (-not $locale) {
    if ($env:MOSH_LOCALE) { $locale = $env:MOSH_LOCALE }
    elseif ($env:LANG)    { $locale = $env:LANG }
    else                  { $locale = 'C.UTF-8' }
}

$serverArgs = @('new')
$serverArgs += @('-c', '256')
if ($localhost -and (-not $bindIp -or $bindIp -match '^ssh$')) {
    # There is no ssh, so there is no $SSH_CONNECTION for -s to read.
    $serverArgs += @('-i', '127.0.0.1')
}
elseif (-not $bindIp -or $bindIp -match '^ssh$') { $serverArgs += '-s' }
elseif ($bindIp -match '^any$') { }
else { $serverArgs += @('-i', $bindIp) }
if ($portRequest) { $serverArgs += @('-p', $portRequest) }
$serverArgs += @('-l', "LANG=$locale", '-l', "LC_ALL=$locale")
if ($command.Count -gt 0) { $serverArgs += @('--') + $command }

# Quoted for the remote POSIX shell -- not for PowerShell and not for cmd.
function ConvertTo-ShQuoted([string[]] $words) {
    ($words | ForEach-Object {
        if ($_ -match '^[A-Za-z0-9_./:@%+=-]+$') { $_ }
        else { "'" + ($_ -replace "'", "'\''") + "'" }
    }) -join ' '
}

$remoteCmd = "$server " + (ConvertTo-ShQuoted $serverArgs)

if ($useRemoteIp -eq 'remote' -and -not $localhost) {
    # Ask the remote end which address ssh actually reached it on, and connect
    # the UDP session to that. Handing the client whatever the user typed
    # breaks for an ssh_config alias, which is not a name DNS can resolve, and
    # on a multi-homed host is not necessarily the address mosh-server bound
    # to. The login shell may not be POSIX, hence the explicit sh -c.
    $probe = '[ -n "$SSH_CONNECTION" ] && printf "\nMOSH SSH_CONNECTION %s\n" "$SSH_CONNECTION"'
    $remoteCmd = 'sh -c ' + (ConvertTo-ShQuoted @($probe)) + ' ; ' + $remoteCmd
}

# ------------------------------------------------------------------- ssh -----

$sshArgs = @()
if ($sshWords.Count -gt 1) { $sshArgs += $sshWords[1..($sshWords.Count - 1)] }
if ($useRemoteIp -eq 'remote') {
    if ($family -eq 'inet') { $sshArgs += '-4' } elseif ($family -eq 'inet6') { $sshArgs += '-6' }
}

# ssh reads stdin and forwards it, which is what makes password and 2FA
# prompts work -- so normally leave it alone. But with no console (a scheduled
# task, a detached process) stdin is not a usable handle and ssh blocks on it
# forever. Detect that and pass -n.
$hasConsole = $true
try { $null = [System.Console]::KeyAvailable } catch { $hasConsole = $false }
if (-not $hasConsole) { $sshArgs += '-n' }

$sshArgs += @($userhost, '--', $remoteCmd)

# The redirection is done by cmd, not by PowerShell. PowerShell turns a native
# program's stderr into error records, and mosh-server writes its banner there.
# Letting cmd do it keeps both streams as plain file handles: stdout to a file
# for us to parse, stderr and stdin straight through to the console.
function ConvertTo-CmdQuoted([string] $s) { '"' + ($s -replace '"', '\"') + '"' }

$outFile = [System.IO.Path]::GetTempFileName()
try {
    if ($localhost) {
        # No ssh at all: start the server here and read its startup line
        # directly. Useful for checking an install without a second machine.
        $serverPath = Find-MoshServer $server
        $cmdLine = (@($serverPath) + $serverArgs | ForEach-Object { ConvertTo-CmdQuoted $_ }) -join ' '
        $cmdLine += ' > ' + (ConvertTo-CmdQuoted $outFile)
        & cmd.exe /c $cmdLine
        $sshExit = $LASTEXITCODE
    } else {
        $cmdLine = (@($sshExe.Source) + $sshArgs | ForEach-Object { ConvertTo-CmdQuoted $_ }) -join ' '
        $cmdLine += ' > ' + (ConvertTo-CmdQuoted $outFile)
        & cmd.exe /c $cmdLine
        $sshExit = $LASTEXITCODE
    }
    $lines = @(Get-Content -LiteralPath $outFile -ErrorAction SilentlyContinue)
} finally {
    Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue
}

# ----------------------------------------------------------------- parsing --

$ip = $null; $port = $null; $key = $null
if ($localhost) { $ip = '127.0.0.1' }
foreach ($line in $lines) {
    $l = ([string]$line).TrimEnd()
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
    if ($l) { Write-Output $l }
}

if (-not $key) {
    if ($sshExit -ne 0) { Die "ssh exited with status $sshExit; no session was started." }
    Die @"
Did not find a 'MOSH CONNECT' line in the server's output.

Usually one of:
  * mosh-server is not installed on the remote host, or not on its PATH
  * the remote locale is not UTF-8 -- try --locale=en_US.UTF-8
  * something in the remote shell's startup files printed before mosh-server ran
"@
}

if (-not $ip) { $ip = $userhost -replace '^.*@', '' }

# ---------------------------------------------------------------- connect ---
# The key goes through the environment and never the command line: command
# lines are readable by every process on the machine and are recorded by WMI,
# Sysmon and Defender.

$env:MOSH_KEY = $key
$env:MOSH_PREDICTION_DISPLAY = $predict
if ($overwrite) { $env:MOSH_PREDICTION_OVERWRITE = 'yes' }
if (-not $termInit) { $env:MOSH_NO_TERM_INIT = '1' }
try {
    & $clientPath $ip $port
    $rc = $LASTEXITCODE
} finally {
    foreach ($v in 'MOSH_KEY', 'MOSH_PREDICTION_OVERWRITE', 'MOSH_NO_TERM_INIT') {
        Remove-Item "Env:\$v" -ErrorAction SilentlyContinue
    }
}
exit $rc
