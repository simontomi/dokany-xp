@echo off
rem Builds dokan2.sys, dokan2.dll, dokanctl.exe and dokan_mirror.exe with the
rem WDK 7.1 (see README.md).
rem Usage: build.cmd [x86^|x64] [fre^|chk]
rem   x86: Windows XP target (32-bit XP, Vista, 7)
rem   x64: Windows Vista target (64-bit Vista, 7)
rem The output is copied to xp\out\<arch>\, the symbols to xp\out\<arch>\pdb\sys
rem and \dll (both are named dokan2.pdb).
setlocal
set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=x86"
set "CFG=%~2"
if "%CFG%"=="" set "CFG=fre"
if not defined WDK71 set "WDK71=C:\WinDDK\7600.16385.1"
if /i "%ARCH%"=="x86" (
  set "OSTGT=WXP"
  set "OBJARCH=i386"
) else (
  set "OSTGT=WLH"
  set "OBJARCH=amd64"
)
set "XPDIR=%~dp0"
call "%WDK71%\bin\setenv.bat" %WDK71%\ %CFG% %ARCH% %OSTGT% no_oacr >nul
if errorlevel 1 exit /b 1

cd /d "%XPDIR%..\sys"
mc.exe -h . -r . dokanfs_msg.mc
if errorlevel 1 exit /b 1

for %%d in (sys dokan dokan_control samples\dokan_mirror) do (
  cd /d "%XPDIR%%%d"
  build -cZw
  if errorlevel 1 (
    echo XPBUILD FAIL %%d
    exit /b 1
  )
)

set "OUT=%XPDIR%out\%ARCH%"
if not exist "%OUT%\pdb\sys" mkdir "%OUT%\pdb\sys"
if not exist "%OUT%\pdb\dll" mkdir "%OUT%\pdb\dll"
set "OBJ=obj%BUILD_ALT_DIR%\%OBJARCH%"
copy /y "%XPDIR%sys\%OBJ%\dokan2.sys" "%OUT%\" >nul || exit /b 1
copy /y "%XPDIR%sys\%OBJ%\dokan2.pdb" "%OUT%\pdb\sys\" >nul
copy /y "%XPDIR%dokan\%OBJ%\dokan2.dll" "%OUT%\" >nul || exit /b 1
copy /y "%XPDIR%dokan\%OBJ%\dokan2.lib" "%OUT%\" >nul
copy /y "%XPDIR%dokan\%OBJ%\dokan2.pdb" "%OUT%\pdb\dll\" >nul
copy /y "%XPDIR%dokan_control\%OBJ%\dokanctl.exe" "%OUT%\" >nul || exit /b 1
copy /y "%XPDIR%samples\dokan_mirror\%OBJ%\dokan_mirror.exe" "%OUT%\" >nul || exit /b 1
echo XPBUILD OK %ARCH% %CFG% -^> %OUT%
exit /b 0
