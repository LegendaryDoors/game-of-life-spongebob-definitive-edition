@echo off
rem Everything is 32-bit, because SBLife.exe is.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VS%" set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VS%" ( echo Could not find vcvars32.bat - install the VS C++ 32-bit toolset & exit /b 1 )
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
