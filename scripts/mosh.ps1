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
       $ProgName update [--check]   install or check for the latest Windows release
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
if ($argv.Count -and $argv[0] -ceq 'update') {
    if ($argv.Count -gt 2 -or ($argv.Count -eq 2 -and $argv[1] -cne '--check')) {
        Die 'Usage: mosh update [--check]'
    }
    try {
        & (Join-Path $PSScriptRoot 'mosh-update.ps1') -Check:($argv.Count -eq 2)
    } catch { Die $_.Exception.Message }
    exit 0
}

if (Test-Path -LiteralPath (Join-Path $PSScriptRoot '.mosh-update.pending')) {
    Die 'An update is in progress or was interrupted. Retry after it finishes, or run mosh update to recover.'
}

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
        '--version'   {
            $versionFile = Join-Path $PSScriptRoot 'mosh-version.txt'
            if (Test-Path -LiteralPath $versionFile -PathType Leaf) {
                Write-Output ("Mosh for Windows " + (Get-Content -LiteralPath $versionFile -Raw).Trim())
            }
            Write-Output $VersionMessage
            exit 0
        }
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
            $sshWords = @([regex]::Matches($s, '"([^"]*)"|''([^'']*)''|(\S+)') | ForEach-Object {
                if ($_.Groups[1].Success) { $_.Groups[1].Value }
                elseif ($_.Groups[2].Success) { $_.Groups[2].Value }
                else { $_.Groups[3].Value }
            })
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

# Accept the documented optional separator after the host.
if ($i -lt $argv.Count -and $argv[$i] -eq '--') { $i++ }
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



function Resolve-MoshAddress([string] $name, [string] $family) {
    try {
        $addresses = @([Net.Dns]::GetHostAddresses($name))
    } catch { Die "Could not resolve ${name}: $_" }
    $v4 = @($addresses | Where-Object AddressFamily -eq InterNetwork)
    $v6 = @($addresses | Where-Object AddressFamily -eq InterNetworkV6)
    switch ($family) {
        'inet'         { $addresses = $v4 }
        'inet6'        { $addresses = $v6 }
        'prefer-inet'  { $addresses = $v4 + $v6 }
        'prefer-inet6' { $addresses = $v6 + $v4 }
        'auto' {
            if ($v4.Count -and $v6.Count) { Die "$name has both IPv4 and IPv6 addresses; select --family" }
        }
    }
    if (-not $addresses.Count) { Die "No $family address for $name" }
    $addresses[0].ToString()
}

$localIp = $null
if (-not $localhost -and $useRemoteIp -eq 'local') {
    $hostName = $userhost -replace '^.*@', ''
    $localIp = Resolve-MoshAddress $hostName $family
}

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
        if ($_ -match '\A[A-Za-z0-9_./:@%+=-]+\z') { $_ }
        else { "'" + ($_ -replace "'", "'\''") + "'" }
    }) -join ' '
}

# Quote for the Windows native argv parser, including runs of backslashes
# before embedded quotes and before the closing quote.
function ConvertTo-NativeQuoted([string] $word) {
    '"' + (($word -replace '(\\*)"', '$1$1\"') -replace '(\\+)\z', '$1$1') + '"'
}

function Invoke-Startup([string] $exe, [string[]] $words, [string] $inputScript) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = ($words | ForEach-Object { ConvertTo-NativeQuoted $_ }) -join ' '
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardInput = [bool]$inputScript
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    try {
        [void]$proc.Start()
        $output = $proc.StandardOutput.ReadToEndAsync()
        if ($inputScript) {
            # A remote without sh may exit before we finish writing.
            try {
                $bytes = [Text.Encoding]::UTF8.GetBytes($inputScript)
                $proc.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
            } catch [IO.IOException] {
            } finally {
                # Windows may report a broken pipe only when Close flushes
                # the last bytes, after the interpreter has already exited.
                try { $proc.StandardInput.BaseStream.Close() } catch [IO.IOException] {}
            }
        }
        $proc.WaitForExit()
        return @{ Output = $output.GetAwaiter().GetResult(); Status = $proc.ExitCode }
    } catch { Die "Could not run ${exe}: $_" }
    finally { $proc.Dispose() }
}

# ssh -n and StdinNull can discard even redirected input. When an
# interpreter exits cleanly without printing the first-line marker, retry
# with the script in the command. Only fixed syntax and encoded bytes are
# exposed to the unknown login shell.
function Invoke-SshBootstrap([string] $interpreter, [string] $scriptText) {
    $result = Invoke-Startup $sshExe.Source ($sshArgs + $interpreter) $scriptText
    if ($result.Status -eq 0 -and [string]::IsNullOrWhiteSpace($result.Output)) {
        if ($interpreter -eq 'sh -s') {
            $octal = ([Text.Encoding]::UTF8.GetBytes($scriptText) | ForEach-Object {
                '\0' + [Convert]::ToString($_, 8).PadLeft(3, '0')
            }) -join ''
            $inline = 'sh -c "printf ''%b'' ''' + $octal + ''' | sh"'
        } else {
            $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($scriptText))
            $inline = "powershell.exe -NoProfile -NonInteractive -EncodedCommand $encoded"
        }
        $result = Invoke-Startup $sshExe.Source ($sshArgs + $inline) ''
    }
    return $result
}

if ($localhost) {
    $startup = Invoke-Startup (Find-MoshServer $server) $serverArgs ''
} else {
    $sshArgs = @()
    if ($sshWords.Count -gt 1) { $sshArgs += $sshWords[1..($sshWords.Count - 1)] }
    if ($family -eq 'inet') { $sshArgs += '-4' } elseif ($family -eq 'inet6') { $sshArgs += '-6' }
    if ($localIp) {
        # Keep the original SSH alias, identity and known_hosts configuration
        # while making SSH and UDP reach the same selected address.
        $sshArgs += @('-o', "HostName=$localIp")
    }
    $sshArgs += @('-T', '--', $userhost)

    # The login shell sees only a fixed command. User arguments are parsed by
    # the explicitly selected interpreter, including when sshd runs cmd.exe.
    $bootstrap = "echo MOSH_POSIX_BOOTSTRAP_V1`n"
    if ($useRemoteIp -eq 'remote') {
        $bootstrap += 'printf "MOSH SSH_CONNECTION %s\n" "$SSH_CONNECTION"' + "`n"
    }
    $bootstrap += "$server " + (ConvertTo-ShQuoted $serverArgs) + "`n"
    $startup = Invoke-SshBootstrap 'sh -s' $bootstrap
    if ($startup.Status -ne 0 -and $startup.Status -ne 255 -and
        $startup.Output -notmatch '(?m)^MOSH_POSIX_BOOTSTRAP_V1\r?$' -and
        $startup.Output -notmatch '(?m)^MOSH CONNECT ') {
        # A stock Windows server has no sh. Once a bootstrap actually ran,
        # never retry it: a command may have side effects even if it fails.
        [Console]::Error.WriteLine('mosh: trying Windows PowerShell on the remote host (SSH may authenticate again)')
        $winScript = 'Write-Output MOSH_WINDOWS_BOOTSTRAP_V1' + "`n" + '$ErrorActionPreference = "Stop"' + "`n"
        if ($useRemoteIp -eq 'remote') {
            $winScript += 'Write-Output ("MOSH SSH_CONNECTION " + $env:SSH_CONNECTION)' + "`n"
        }
        # Send data on stdin so a long command never has to fit through
        # cmd.exe's command line. ASCII transport also works with the
        # legacy input code pages of Windows PowerShell 5.1.
        $request = @{
            Server = $server
            Arguments = ($serverArgs | ForEach-Object { ConvertTo-NativeQuoted $_ }) -join ' '
        } | ConvertTo-Json -Compress
        $data = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($request))
        $winScript += "`$request = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('$data')) | ConvertFrom-Json" + "`n"
        $winScript += @'
$si = New-Object Diagnostics.ProcessStartInfo
$si.FileName = $request.Server
$si.Arguments = $request.Arguments
if (!(Test-Path -LiteralPath $si.FileName -PathType Leaf) -and $request.Server -match '^\s*(?:"([^"]+)"|(\S+))\s+(.+)$') {
    $si.FileName = $Matches[1] + $Matches[2]
    $si.Arguments = $Matches[3] + ' ' + $si.Arguments
}

$si.UseShellExecute = $false
try {
    $child = [Diagnostics.Process]::Start($si)
    $child.WaitForExit()
    exit $child.ExitCode
} catch {
    [Console]::Error.WriteLine($_)
    exit 1
}

'@
        $startup = Invoke-SshBootstrap 'powershell.exe -NoLogo -NoProfile -NonInteractive -Command -' ($winScript + "`n")
    }
}

$sshExit = $startup.Status
$lines = @($startup.Output -split "`r?`n")

# ----------------------------------------------------------------- parsing --

$ip = $null; $port = $null; $key = $null
if ($localhost) { $ip = '127.0.0.1' }
foreach ($line in $lines) {
    $l = ([string]$line).TrimEnd()
    if ($l -cin @('MOSH_POSIX_BOOTSTRAP_V1', 'MOSH_WINDOWS_BOOTSTRAP_V1')) { continue }
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

if ($localIp) { $ip = $localIp }
if (-not $ip) { $ip = Resolve-MoshAddress ($userhost -replace '^.*@', '') $family }

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
