@echo off
rem Run the BigBrother IPC access probe (see check_ipc_access.py).
rem Use an UNELEVATED prompt to test the DACL, and an ELEVATED prompt to test
rem the client-backend peer check.

where py >nul 2>nul
if %errorlevel%==0 (
    py -3 "%~dp0check_ipc_access.py"
    goto :eof
)

where python >nul 2>nul
if %errorlevel%==0 (
    python "%~dp0check_ipc_access.py"
    goto :eof
)

echo Could not find a Python interpreter. Install Python 3 or edit this script.
exit /b 1