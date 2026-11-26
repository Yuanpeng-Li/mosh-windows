winget packaging
================

Manifests for `YuanpengLi.MoshWindows`.

Layout follows the winget-pkgs convention exactly, so the version directory can
be copied straight into a fork of that repository:

```
manifests/y/YuanpengLi/MoshWindows/<version>/
    YuanpengLi.MoshWindows.yaml               version manifest
    YuanpengLi.MoshWindows.installer.yaml     URL, hash, portable alias
    YuanpengLi.MoshWindows.locale.en-US.yaml  description, license, tags
```

The package installs all three programs — `mosh.exe` (the launcher),
`mosh-client.exe` and `mosh-server.exe` — plus `mosh.ps1` and `mosh.cmd`. Each
of the three gets a `PortableCommandAlias`, and `mosh-server` needs its own
because the client asks ssh to run it by that name.

This paragraph used to say the package shipped the launcher alone and that
`mosh --setup` fetched a third-party client. That stopped being true when this
fork gained a real `mosh-client.exe` and `mosh-server.exe`, and the description
did not follow. It is recorded here because it is the kind of drift the
"Cutting a new version" section below now exists to prevent.

`NestedInstallerType: portable` requires an `.exe`; a `.cmd` is rejected with
*"The file type of the referenced file is not allowed"*. That is one of the
reasons `mosh.exe` exists.

Testing locally
---------------

```powershell
winget validate --manifest packaging\winget\manifests\y\YuanpengLi\MoshWindows\0.2.0

winget settings --enable LocalManifestFiles     # admin, once
winget install --manifest packaging\winget\manifests\y\YuanpengLi\MoshWindows\0.2.0
winget settings --disable LocalManifestFiles    # put it back

mosh --local localhost    # the install is complete; no --setup step
mosh user@host
```

Cutting a new version
---------------------

1. Push a stable Windows tag such as `vX.Y.Z` on the commit to release.
   The Windows workflow builds the x64 binaries, tests sessions, installation
   and self-update, then publishes a GitHub Release after the Windows and
   POSIX jobs pass.
2. The release contains `mosh-windows-x64-<tag>.zip`, the three compiled
   executables individually, and `SHA256SUMS`. The ZIP keeps its top-level
   `mosh/` folder for WinGet and includes the scripts and release version.
3. Copy the previous WinGet manifest directory, set `PackageVersion` and
   `ReleaseDate`, and use the versioned ZIP URL and its SHA-256 from the
   release. Run `winget validate` and test an install before submitting.

Submitting to the public winget repository
------------------------------------------

`winget install YuanpengLi.MoshWindows` only resolves once the manifests are
merged into [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs).
Until then, use the local-manifest path above or the one-line installer.

```powershell
# wingetcreate handles the fork, branch, commit and PR
winget install Microsoft.WingetCreate
wingetcreate submit packaging\winget\manifests\y\YuanpengLi\MoshWindows\0.2.0
```

What review checks, and where this package stands:

| | |
|---|---|
| installer URL is stable and versioned | GitHub release asset |
| SHA-256 matches | verified against the published asset |
| manifest schema valid | `winget validate` passes |
| installs and uninstalls cleanly | tested; portable alias `mosh` |
| no bundled third-party binaries of unclear provenance | the binaries are built from the tagged source |
| unsigned binary | `mosh.exe` is not code-signed — expect SmartScreen warnings, and a reviewer may ask |

Code signing is the one gap. It needs a certificate; without one, SmartScreen
will warn on first run until the binary builds reputation.
