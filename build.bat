@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo [primes] Suche MSVC-Buildtools...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Fehler: vswhere.exe nicht gefunden. Visual Studio Build Tools fehlen.
    exit /b 1
)

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo Fehler: MSVC C++-Buildtools nicht gefunden.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo Fehler: vcvars64.bat konnte nicht geladen werden.
    exit /b 1
)

echo [primes] Kompiliere primes.c (x64, /O2, statische CRT)...
cl /nologo /O2 /GL /MT /W4 /std:c11 /DNDEBUG primes.c ^
   /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:CONSOLE /INCREMENTAL:NO /OUT:primes.exe
if errorlevel 1 (
    echo [primes] Build fehlgeschlagen.
    exit /b 1
)

del /q primes.obj >nul 2>&1
echo [primes] Fertig: primes.exe
for %%F in (primes.exe) do echo [primes] Groesse: %%~zF Bytes
endlocal
