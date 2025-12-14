@echo off
echo Starting Distributed File System...
echo.

REM Check if executables exist
if not exist "coordinator.exe" (
    echo ERROR: coordinator.exe not found. Please build first using build.bat
    exit /b 1
)
if not exist "node.exe" (
    echo ERROR: node.exe not found. Please build first using build.bat
    exit /b 1
)
if not exist "client.exe" (
    echo ERROR: client.exe not found. Please build first using build.bat
    exit /b 1
)

echo Starting coordinator in background...
start "DFS Coordinator" coordinator.exe

REM Wait a bit for coordinator to start
timeout /t 2 /nobreak >nul

echo Starting node 1 in background...
start "DFS Node 1" node.exe 1

timeout /t 1 /nobreak >nul

echo Starting node 2 in background...
start "DFS Node 2" node.exe 2

timeout /t 1 /nobreak >nobreak >nul

echo.
echo ========================================
echo DFS System Started!
echo ========================================
echo Coordinator: Running on port 9000
echo Node 1: Running on port 9001
echo Node 2: Running on port 9002
echo.
echo You can now use the client:
echo   client.exe upload test.txt /docs/test.txt
echo   client.exe download /docs/test.txt output.txt
echo   client.exe list
echo.
echo To stop the system, close the coordinator and node windows
echo or use: taskkill /IM coordinator.exe /F
echo         taskkill /IM node.exe /F
echo.

