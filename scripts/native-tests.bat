@echo off
:: Build and run the native (no-board) test suites with MSVC.
:: Needs isystem_dsp_kernels.h in build\ - export it from CraftAudio.
setlocal
set ROOT=%~dp0..
call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist "%ROOT%\build" mkdir "%ROOT%\build"
pushd "%ROOT%"

cl /nologo /std:c++17 /EHsc /I include /I build ^
   tests\test_stream_graph.cpp src\AudioStreamGraph.cpp ^
   /Fo:build\ /Fe:build\test_stream_graph.exe >nul || goto :fail
build\test_stream_graph.exe || goto :fail

cl /nologo /std:c++17 /EHsc /I include /I build ^
   tests\test_stream_patch.cpp src\AudioStreamPatch.cpp src\AudioStreamGraph.cpp ^
   /Fo:build\ /Fe:build\test_stream_patch.exe >nul || goto :fail
build\test_stream_patch.exe || goto :fail

cl /nologo /std:c++17 /EHsc /I include /I build ^
   tests\test_codec.cpp src\AudioCodecES8388.cpp ^
   /Fo:build\ /Fe:build\test_codec.exe >nul || goto :fail
build\test_codec.exe || goto :fail

cl /nologo /std:c++17 /EHsc /I include /I build ^
   tests\test_stream_control.cpp src\AudioStreamControl.cpp ^
   src\AudioStreamPatch.cpp src\AudioStreamGraph.cpp ^
   /Fo:build\ /Fe:build\test_stream_control.exe >nul || goto :fail
build\test_stream_control.exe || goto :fail

if exist tests\dump_graph_vectors.cpp (
  cl /nologo /std:c++17 /EHsc /I include /I build ^
     tests\dump_graph_vectors.cpp src\AudioStreamGraph.cpp ^
     /Fo:build\ /Fe:build\dump_graph_vectors.exe >nul || goto :fail
)

if exist tests\dump_control_vectors.cpp (
  cl /nologo /std:c++17 /EHsc /I include /I build ^
     tests\dump_control_vectors.cpp src\AudioStreamControl.cpp ^
     src\AudioStreamPatch.cpp src\AudioStreamGraph.cpp ^
     /Fo:build\ /Fe:build\dump_control_vectors.exe >nul || goto :fail
)

popd
echo ALL NATIVE SUITES PASSED
exit /b 0

:fail
popd
echo NATIVE SUITE FAILED
exit /b 1
