@echo off
REM Compila VOX DNA PRO en Windows (requiere Visual Studio 2022 con "Desarrollo para escritorio con C++" y CMake)
cmake -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto err
cmake --build build --config Release --parallel
if errorlevel 1 goto err
echo.
echo OK: build\VoxDnaPro_artefacts\Release\VST3\VOX DNA PRO.vst3
echo Copialo a C:\Program Files\Common Files\VST3\
pause
exit /b 0
:err
echo Error de compilacion
pause
exit /b 1
