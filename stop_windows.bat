@echo off
echo Stopping Distributed File System...
echo.

echo Stopping coordinator...
taskkill /IM coordinator.exe /F >nul 2>&1

echo Stopping nodes...
taskkill /IM node.exe /F >nul 2>&1

echo.
echo DFS system stopped.
echo You can verify with: tasklist | findstr /I "coordinator node"

