@echo off
rem mosh -- the mobile shell. Fallback shim for when mosh.exe is not built.
rem
rem Prefer mosh.exe: batch cannot quote arguments for PowerShell's parser, so
rem an argument containing a space -- --client="C:\Program Files\x.exe" -- gets
rem split here. mosh.exe handles that correctly and is what the packages ship.
setlocal
if exist "%~dp0mosh.exe" (
  "%~dp0mosh.exe" %*
  exit /b %ERRORLEVEL%
)
set "MOSH_PS1=%~dp0mosh.ps1"
if not exist "%MOSH_PS1%" (
  echo mosh: cannot find mosh.ps1 or mosh.exe next to %~f0 1>&2
  exit /b 1
)
where pwsh >nul 2>&1 && (
  pwsh -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "& '%MOSH_PS1%' @args" -- %*
) || (
  powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "& '%MOSH_PS1%' @args" -- %*
)
exit /b %ERRORLEVEL%
