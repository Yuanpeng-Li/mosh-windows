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

1. Do not build the archive by hand. `.github/workflows/windows.yml` assembles
   it on every push, checks its contents against the list the manifests name,
   and uploads it as the `mosh-windows-x64` artifact with its SHA-256 in the
   log. Take that artifact.

   The archive has one top-level `mosh\` directory — which is what makes
   `NestedInstallerFiles`' `mosh\mosh.exe` resolve — containing `mosh.exe`,
   `mosh-client.exe`, `mosh-server.exe`, `mosh.ps1`, `mosh.cmd`, `COPYING` and
   `README.windows.md`.

2. Rename it to `mosh-windows-x64-<tag>.zip` and attach it to the GitHub
   release for `<tag>`.

4. Copy the previous version directory to the new version number and update
   `PackageVersion` in all three files, plus `InstallerUrl`, `InstallerSha256`
   (uppercase hex) and `ReleaseDate` in the installer manifest.

5. `winget validate`, then install from the local manifest and actually run
   `mosh user@host` before publishing. A manifest can validate and still install
   something that does not work.

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
| no bundled third-party binaries of unclear provenance | client is fetched, not bundled |
| unsigned binary | `mosh.exe` is not code-signed — expect SmartScreen warnings, and a reviewer may ask |

Code signing is the one gap. It needs a certificate; without one, SmartScreen
will warn on first run until the binary builds reputation.
