@echo off
echo Building Distributed File System...
echo.

echo Building coordinator...
g++ -std=c++17 coordinator/coordinator.cpp -o coordinator.exe -lws2_32
if %errorlevel% neq 0 (
    echo ERROR: Failed to build coordinator
    exit /b 1
)

echo Building node...
g++ -std=c++17 node/node.cpp -o node.exe -lws2_32
if %errorlevel% neq 0 (
    echo ERROR: Failed to build node
    exit /b 1
)

echo Building client...
g++ -std=c++17 client/client.cpp -o client.exe -lws2_32
if %errorlevel% neq 0 (
    echo ERROR: Failed to build client
    exit /b 1
)

echo.
echo Build successful! Executables created:
echo   - coordinator.exe
echo   - node.exe
echo   - client.exe
echo.
echo To run the system:
echo   1. Start coordinator: coordinator.exe
echo   2. Start nodes: node.exe 1  (in separate terminals)
echo                   node.exe 2
echo   3. Use client: client.exe upload test.txt /docs/test.txt
echo.

