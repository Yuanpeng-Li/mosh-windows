winget packaging
================

Manifests for `YuanpengLi.MoshWindows`, the Windows launcher.

Layout follows the winget-pkgs convention exactly, so the version directory can
be copied straight into a fork of that repository:

```
manifests/y/YuanpengLi/MoshWindows/<version>/
    YuanpengLi.MoshWindows.yaml               version manifest
    YuanpengLi.MoshWindows.installer.yaml     URL, hash, portable alias
    YuanpengLi.MoshWindows.locale.en-US.yaml  description, license, tags
```

The package installs the launcher only — `mosh.exe`, `mosh.ps1`, `mosh.cmd`.
It deliberately does **not** bundle `mosh-client.exe`: that binary belongs to
[MoshCatty](https://github.com/binaricat/MoshCatty), a separate GPLv3 project,
and redistributing it would put the obligation to supply matching source on
this repository. `mosh --setup` downloads it from its own release and checks it
against a pinned SHA-256.

`NestedInstallerType: portable` requires an `.exe`; a `.cmd` is rejected with
*"The file type of the referenced file is not allowed"*. That is one of the
reasons `mosh.exe` exists.

Testing locally
---------------

```powershell
winget validate --manifest packaging\winget\manifests\y\YuanpengLi\MoshWindows\0.1.0

winget settings --enable LocalManifestFiles     # admin, once
winget install --manifest packaging\winget\manifests\y\YuanpengLi\MoshWindows\0.1.0
winget settings --disable LocalManifestFiles    # put it back

mosh --setup
mosh user@host
```

Cutting a new version
---------------------

1. Build `mosh.exe`:

   ```
   cl /nologo /EHsc /std:c++17 /O2 /W3 scripts\mosh-launcher.cc /Fe:mosh.exe shell32.lib
   ```

2. Assemble the archive with a single top-level `mosh\` directory containing
   `mosh.exe`, `mosh.ps1`, `mosh.cmd`, `README.txt` and `COPYING.txt`, named
   `mosh-windows-launcher-<tag>.zip`.

3. Attach it to the GitHub release for `<tag>`.

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
wingetcreate submit packaging\winget\manifests\y\YuanpengLi\MoshWindows\0.1.0
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
