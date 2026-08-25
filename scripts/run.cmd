@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

call "%~dp0build.cmd"
if errorlevel 1 exit /b 1

set "PATH=D:\tool\mingw64\bin;%VCPKG_ROOT%\installed\x64-mingw-dynamic\bin;%PATH%"
if not exist "build\RenderLab.exe" (
  echo ERROR: build\RenderLab.exe not found
  exit /b 1
)

echo.
echo === running build\RenderLab.exe ===
"build\RenderLab.exe"
set "EC=%ERRORLEVEL%"
echo.
echo === exited: %EC% ===
exit /b %EC%
