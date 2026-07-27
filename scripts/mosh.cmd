@echo off
rem mosh -- the mobile shell. Shim so `mosh user@host` works from cmd.exe and
rem from anywhere on PATH, not only from a PowerShell function.
rem
rem -Command with the call operator is used rather than -File on purpose:
rem -File's argument parser splits "--client=C:\path" at the colon, treating it
rem as the -Name:Value parameter syntax.
setlocal
set "MOSH_PS1=%~dp0mosh.ps1"
if not exist "%MOSH_PS1%" (
  echo mosh: cannot find mosh.ps1 next to %~f0 1>&2
  exit /b 1
)
where pwsh >nul 2>&1 && (
  pwsh -NoLogo -NoProfile -Command "& '%MOSH_PS1%' @args" -- %*
) || (
  powershell -NoLogo -NoProfile -Command "& '%MOSH_PS1%' @args" -- %*
)
exit /b %ERRORLEVEL%
