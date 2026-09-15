@echo off
setlocal
set "FORGE_VS="
set "FORGE_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%FORGE_VSWHERE%" goto missing
for /f "usebackq delims=" %%i in (`"%FORGE_VSWHERE%" -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FORGE_VS=%%i"
if not defined FORGE_VS goto missing
call "%FORGE_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 goto missing
if exist "%FORGE_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "PATH=%FORGE_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
if exist "%FORGE_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "PATH=%FORGE_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
where cl >nul 2>&1
if errorlevel 1 goto missing
where cmake >nul 2>&1
if errorlevel 1 goto missing
where ninja >nul 2>&1
if errorlevel 1 goto missing
if /i "%~1"=="--check" exit /b 0
call "%~dp0Run-Forge.cmd"
exit /b %ERRORLEVEL%
:missing
echo Gameplay compilation needs Visual Studio 2022 C++ tools, CMake 3.24+ and Ninja.
echo Install the Desktop development with C++ workload and C++ CMake tools.
echo You can still open the editor using Run-Forge.cmd.
if /i not "%~1"=="--check" pause
exit /b 1
