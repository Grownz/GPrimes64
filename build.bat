@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo [GPrimes64] Locating MSVC build tools...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Error: vswhere.exe not found. Visual Studio Build Tools are missing.
    exit /b 1
)

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo Error: MSVC C++ build tools not found.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo Error: could not load vcvars64.bat.
    exit /b 1
)

echo [GPrimes64] Compiling primes.c (x64, /O2, static CRT)...
cl /nologo /O2 /GL /MT /W4 /std:c11 /DNDEBUG primes.c ^
   /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:CONSOLE /INCREMENTAL:NO /OUT:gprimes64.exe
if errorlevel 1 (
    echo [GPrimes64] Build failed.
    exit /b 1
)

del /q primes.obj >nul 2>&1
echo [GPrimes64] Done: gprimes64.exe
for %%F in (gprimes64.exe) do echo [GPrimes64] Size: %%~zF bytes
endlocal
