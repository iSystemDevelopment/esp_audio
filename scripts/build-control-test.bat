@echo off
:: Build + run only the control suite. Used by the negative-control loop.
setlocal
set ROOT=%~dp0..
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
pushd "%ROOT%"
if not exist build\mut mkdir build\mut
cl /nologo /std:c++17 /EHsc /I include /I build ^
   tests\test_stream_control.cpp src\AudioStreamControl.cpp ^
   src\AudioStreamPatch.cpp src\AudioStreamGraph.cpp ^
   /Fo:build\mut\ /Fe:build\test_mut_ctl.exe >nul
if errorlevel 1 (
  popd
  echo BUILD FAILED
  exit /b 2
)
build\test_mut_ctl.exe
popd
exit /b 0
