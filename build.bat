@echo off
rem Everything is 32-bit, because SBLife.exe is.
setlocal
call :findvs
if not exist "%VS%" ( echo Could not find vcvars32.bat - install the VS C++ 32-bit toolset & exit /b 1 )
rem Some installs print "'vswhere.exe' is not recognized" on the next line. That
rem comes from inside Microsoft's vcvars32.bat, not from here, and is harmless.
call "%VS%" >nul || ( echo VCVARS FAILED & exit /b 1 )

cd /d "%~dp0"
if not exist build mkdir build

set CFLAGS=/nologo /O2 /MT /W3 /Isrc /Ilauncher /D_CRT_SECURE_NO_WARNINGS
set LIBS=user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib advapi32.lib

cl %CFLAGS% /LD src\dllmain.cpp ^
   /Fo:build\ /Fe:build\d3d9.dll ^
   /link /DEF:src\d3d9.def user32.lib gdi32.lib psapi.lib winmm.lib
if errorlevel 1 ( echo BUILD FAILED ^(d3d9.dll^) & exit /b 1 )

rem launcher.rc embeds build\d3d9.dll, so the DLL must exist before rc runs.
rc /nologo /i launcher /fo build\launcher.res launcher\launcher.rc
if errorlevel 1 ( echo BUILD FAILED ^(resources^) & exit /b 1 )

rem The .res goes after /link, or /TC makes cl compile it as C source.
cl %CFLAGS% /TC launcher\launcher.c ^
   /Fo:build\ /Fe:build\GameOfLifeDefinitiveEdition.exe ^
   /link /SUBSYSTEM:WINDOWS build\launcher.res %LIBS%
if errorlevel 1 ( echo BUILD FAILED ^(launcher^) & exit /b 1 )

echo BUILD OK -^> build\d3d9.dll
echo BUILD OK -^> build\GameOfLifeDefinitiveEdition.exe
exit /b 0

rem --- finding the toolset -------------------------------------------------
rem vswhere ships with every VS 2017+ installer and is the only reliable way to
rem locate an install: it covers Community, Professional, Enterprise and the
rem standalone Build Tools, at whatever path the user chose.
:findvs
set "VS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
rem 32-bit Windows has no ProgramFiles(x86), so fall back to the plain root.
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i\VC\Auxiliary\Build\vcvars32.bat"
if exist "%VS%" goto :eof

rem No vswhere, or it found nothing. Walk the default layouts instead. Oldest
rem first and edition-preference last, so the newest match is what survives.
for %%r in ("%ProgramFiles%" "%ProgramFiles(x86)%") do for %%y in (2017 2019 2022) do for %%e in (BuildTools Enterprise Professional Community) do if exist "%%~r\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvars32.bat" set "VS=%%~r\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvars32.bat"
goto :eof
