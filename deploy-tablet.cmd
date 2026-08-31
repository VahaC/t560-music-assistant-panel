@echo off
setlocal

pushd "%~dp0" || (
    echo ERROR: Cannot open the project directory.
    pause
    exit /b 1
)

set "TABLET_IP=%T560_TABLET_IP%"
if not "%~1"=="" set "TABLET_IP=%~1"
if not defined TABLET_IP set "TABLET_IP=192.168.1.105"

set "TABLET_USER=%T560_TABLET_USER%"
if not defined TABLET_USER set "TABLET_USER=vahac"
set "TABLET_TARGET=%TABLET_USER%@%TABLET_IP%"
set "REMOTE_HOME=/home/%TABLET_USER%"
set "REMOTE_BIN=%REMOTE_HOME%/.local/bin"
set "REMOTE_STATE=%REMOTE_HOME%/.local/state"
set /p CHECKSUM_LINE=<"SHA256SUMS"
set "LOCAL_SHA256=%CHECKSUM_LINE:~0,64%"

set "SSH_EXE="
for /f "delims=" %%I in ('where ssh.exe 2^>nul') do if not defined SSH_EXE set "SSH_EXE=%%I"
if not defined SSH_EXE if exist "%WINDIR%\Sysnative\OpenSSH\ssh.exe" set "SSH_EXE=%WINDIR%\Sysnative\OpenSSH\ssh.exe"
if not defined SSH_EXE if exist "%WINDIR%\System32\OpenSSH\ssh.exe" set "SSH_EXE=%WINDIR%\System32\OpenSSH\ssh.exe"
if not defined SSH_EXE if exist "C:\Windows\Sysnative\OpenSSH\ssh.exe" set "SSH_EXE=C:\Windows\Sysnative\OpenSSH\ssh.exe"
if not defined SSH_EXE if exist "C:\Windows\System32\OpenSSH\ssh.exe" set "SSH_EXE=C:\Windows\System32\OpenSSH\ssh.exe"
if not defined SSH_EXE if defined ProgramW6432 if exist "%ProgramW6432%\Git\usr\bin\ssh.exe" set "SSH_EXE=%ProgramW6432%\Git\usr\bin\ssh.exe"
if not defined SSH_EXE if exist "%ProgramFiles%\Git\usr\bin\ssh.exe" set "SSH_EXE=%ProgramFiles%\Git\usr\bin\ssh.exe"
if not defined SSH_EXE (
    echo ERROR: ssh.exe was not found.
    echo Windows directory: %WINDIR%
    echo Process architecture: %PROCESSOR_ARCHITECTURE%
    goto :failure
)
echo Using SSH client: %SSH_EXE%

for %%F in (
    "t560-panel"
    "scripts\t560-panel-watchdog"
    "scripts\t560-open-panel"
    "scripts\t560-restart-panel"
    "scripts\t560-power-button.py"
) do (
    if not exist "%%~F" (
        echo ERROR: Missing local file: %%~F
        goto :failure
    )
)

echo Deploying the T560 panel to %TABLET_TARGET%...
"%SSH_EXE%" "%TABLET_TARGET%" "mkdir -p '%REMOTE_BIN%' '%REMOTE_STATE%'"
if errorlevel 1 goto :failure

call :send_file "t560-panel" "%REMOTE_BIN%/t560-panel.new"
if errorlevel 1 goto :failure
call :send_file "scripts\t560-panel-watchdog" "%REMOTE_BIN%/t560-panel-watchdog.new"
if errorlevel 1 goto :failure
call :send_file "scripts\t560-open-panel" "%REMOTE_BIN%/t560-open-panel.new"
if errorlevel 1 goto :failure
call :send_file "scripts\t560-restart-panel" "%REMOTE_BIN%/t560-restart-panel.new"
if errorlevel 1 goto :failure
call :send_file "scripts\t560-power-button.py" "%REMOTE_BIN%/t560-power-button.py.new"
if errorlevel 1 goto :failure

echo Validating runtime dependencies...
"%SSH_EXE%" "%TABLET_TARGET%" "set -eu; chmod 755 '%REMOTE_BIN%/t560-panel.new' '%REMOTE_BIN%/t560-panel-watchdog.new' '%REMOTE_BIN%/t560-open-panel.new' '%REMOTE_BIN%/t560-restart-panel.new' '%REMOTE_BIN%/t560-power-button.py.new'; python3 -m py_compile '%REMOTE_BIN%/t560-power-button.py.new'; ldd '%REMOTE_BIN%/t560-panel.new' > '%REMOTE_STATE%/t560-deploy-ldd.log' 2>&1; cat '%REMOTE_STATE%/t560-deploy-ldd.log'; if grep -q 'not found' '%REMOTE_STATE%/t560-deploy-ldd.log'; then echo 'ERROR: A runtime dependency is missing.' >&2; exit 1; fi"
if errorlevel 1 goto :failure

echo Activating the update and restarting the panel...
"%SSH_EXE%" "%TABLET_TARGET%" "pkill -f '[t]560-panel-watchdog'" >nul 2>&1
"%SSH_EXE%" "%TABLET_TARGET%" "pkill -x t560-panel" >nul 2>&1
"%SSH_EXE%" "%TABLET_TARGET%" "pkill -f '[t]560-power-button.py'" >nul 2>&1

"%SSH_EXE%" "%TABLET_TARGET%" "cp '%REMOTE_BIN%/t560-panel' '%REMOTE_BIN%/t560-panel.previous'; cp '%REMOTE_BIN%/t560-panel-watchdog' '%REMOTE_BIN%/t560-panel-watchdog.previous'; cp '%REMOTE_BIN%/t560-open-panel' '%REMOTE_BIN%/t560-open-panel.previous'; cp '%REMOTE_BIN%/t560-restart-panel' '%REMOTE_BIN%/t560-restart-panel.previous'; cp '%REMOTE_BIN%/t560-power-button.py' '%REMOTE_BIN%/t560-power-button.py.previous'"
if errorlevel 1 goto :failure

"%SSH_EXE%" "%TABLET_TARGET%" "mv '%REMOTE_BIN%/t560-panel.new' '%REMOTE_BIN%/t560-panel'; mv '%REMOTE_BIN%/t560-panel-watchdog.new' '%REMOTE_BIN%/t560-panel-watchdog'; mv '%REMOTE_BIN%/t560-open-panel.new' '%REMOTE_BIN%/t560-open-panel'; mv '%REMOTE_BIN%/t560-restart-panel.new' '%REMOTE_BIN%/t560-restart-panel'; mv '%REMOTE_BIN%/t560-power-button.py.new' '%REMOTE_BIN%/t560-power-button.py'; chmod 755 '%REMOTE_BIN%/t560-panel' '%REMOTE_BIN%/t560-panel-watchdog' '%REMOTE_BIN%/t560-open-panel' '%REMOTE_BIN%/t560-restart-panel' '%REMOTE_BIN%/t560-power-button.py'"
if errorlevel 1 goto :rollback

"%SSH_EXE%" "%TABLET_TARGET%" "'%REMOTE_BIN%/t560-restart-panel'"
if errorlevel 1 goto :rollback
"%SSH_EXE%" "%TABLET_TARGET%" "nohup python3 '%REMOTE_BIN%/t560-power-button.py' >>'%REMOTE_STATE%/power-button.log' 2>&1 </dev/null &"
if errorlevel 1 goto :rollback

powershell.exe -NoProfile -Command "Start-Sleep -Seconds 4"
"%SSH_EXE%" "%TABLET_TARGET%" "pgrep -x t560-panel; pgrep -f '[t]560-panel-watchdog'; pgrep -f '[t]560-power-button.py'; tail -n 20 '%REMOTE_STATE%/t560-music-panel.log'; tail -n 5 '%REMOTE_STATE%/power-button.log'"
if errorlevel 1 goto :rollback

set "REMOTE_SHA_FILE=%TEMP%\t560-remote-sha256-%RANDOM%-%RANDOM%.txt"
"%SSH_EXE%" "%TABLET_TARGET%" "sha256sum '%REMOTE_BIN%/t560-panel'" >"%REMOTE_SHA_FILE%"
if errorlevel 1 (
    del /q "%REMOTE_SHA_FILE%" >nul 2>&1
    goto :rollback
)
set "REMOTE_SHA256="
for /f "usebackq tokens=1" %%H in ("%REMOTE_SHA_FILE%") do if not defined REMOTE_SHA256 set "REMOTE_SHA256=%%H"
del /q "%REMOTE_SHA_FILE%" >nul 2>&1
if /i not "%REMOTE_SHA256%"=="%LOCAL_SHA256%" goto :rollback
echo Verified SHA256: %REMOTE_SHA256%

echo.
echo Deployment complete. The previous files are available with the .previous suffix.
popd
pause
exit /b 0

:rollback
echo ERROR: The updated panel did not pass the runtime check. Restoring the previous version.
"%SSH_EXE%" "%TABLET_TARGET%" "pkill -f '[t]560-panel-watchdog'" >nul 2>&1
"%SSH_EXE%" "%TABLET_TARGET%" "pkill -x t560-panel" >nul 2>&1
"%SSH_EXE%" "%TABLET_TARGET%" "pkill -f '[t]560-power-button.py'" >nul 2>&1
"%SSH_EXE%" "%TABLET_TARGET%" "mv '%REMOTE_BIN%/t560-panel.previous' '%REMOTE_BIN%/t560-panel'; mv '%REMOTE_BIN%/t560-panel-watchdog.previous' '%REMOTE_BIN%/t560-panel-watchdog'; mv '%REMOTE_BIN%/t560-open-panel.previous' '%REMOTE_BIN%/t560-open-panel'; mv '%REMOTE_BIN%/t560-restart-panel.previous' '%REMOTE_BIN%/t560-restart-panel'; mv '%REMOTE_BIN%/t560-power-button.py.previous' '%REMOTE_BIN%/t560-power-button.py'; chmod 755 '%REMOTE_BIN%/t560-panel' '%REMOTE_BIN%/t560-panel-watchdog' '%REMOTE_BIN%/t560-open-panel' '%REMOTE_BIN%/t560-restart-panel' '%REMOTE_BIN%/t560-power-button.py'"
"%SSH_EXE%" "%TABLET_TARGET%" "'%REMOTE_BIN%/t560-restart-panel'"
"%SSH_EXE%" "%TABLET_TARGET%" "nohup python3 '%REMOTE_BIN%/t560-power-button.py' >>'%REMOTE_STATE%/power-button.log' 2>&1 </dev/null &"
goto :failure

:send_file
echo Transferring %~1...
set "T560_LOCAL_FILE=%~f1"
powershell.exe -NoProfile -Command "[Convert]::ToBase64String([IO.File]::ReadAllBytes($env:T560_LOCAL_FILE))" | "%SSH_EXE%" "%TABLET_TARGET%" "base64 -d > '%~2'"
set "TRANSFER_EXIT=%ERRORLEVEL%"
set "T560_LOCAL_FILE="
if not "%TRANSFER_EXIT%"=="0" (
    echo ERROR: Transfer failed: %~1
    exit /b 1
)
exit /b 0

:failure
echo ERROR: Deployment failed. The currently installed version was not replaced unless activation had already started.
popd
pause
exit /b 1
