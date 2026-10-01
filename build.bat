@echo off
setlocal
cmake -S . -B build\windows -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DAUTOSZUWEB_BUILD_TESTS=ON
if errorlevel 1 exit /b %errorlevel%
cmake --build build\windows --parallel
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir build\windows --output-on-failure
if errorlevel 1 exit /b %errorlevel%
echo.
echo Build complete: build\windows\AutoSZUWeb.exe
pause
