@echo off
rem ===========================================================================
rem  Compila I-CPU en Windows con MSVC (Visual Studio / Build Tools 2022+).
rem
rem  Uso:  scripts\build.bat [release|debug] [test] [clean]
rem    release  (por defecto) compilacion optimizada en build\msvc-release
rem    debug    compilacion con depuracion en build\msvc-debug
rem    test     ejecuta las pruebas al terminar
rem    clean    borra el directorio de compilacion antes de empezar
rem
rem  Por que MSVC: Smart App Control de Windows bloquea binarios sin firma
rem  reconocida (p. ej. as.exe/ld.exe de muchas distribuciones de MinGW).
rem  MSVC, y el CMake y Ninja que incluye Visual Studio, estan firmados por
rem  Microsoft. Ver docs\tecnico\decisiones-de-diseno.md (D1)
rem ===========================================================================
setlocal
rem Antes de "shift": shift tambien desplaza %0 y %~dp0 dejaria de ser esta carpeta
set "ROOT=%~dp0.."

set "CONFIG=release"
set "RUN_TESTS=0"
set "CLEAN=0"
:args
if "%~1"=="" goto args_done
if /i "%~1"=="release" set "CONFIG=release"
if /i "%~1"=="debug" set "CONFIG=debug"
if /i "%~1"=="test" set "RUN_TESTS=1"
if /i "%~1"=="clean" set "CLEAN=1"
shift
goto args
:args_done
set "PRESET=msvc-%CONFIG%"

rem --- Entorno de MSVC (solo si no se ha cargado ya) ---
where cl >nul 2>nul
if not errorlevel 1 goto have_msvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" goto find_vs
echo [build] No se encuentra Visual Studio. Instala "Build Tools para Visual Studio"
echo         con la carga de trabajo "Desarrollo para el escritorio con C++".
exit /b 1
:find_vs
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if defined VSDIR goto load_vs
echo [build] Visual Studio no tiene instalado el compilador de C++ (MSVC).
exit /b 1
:load_vs
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:have_msvc

cd /d "%ROOT%"
if "%CLEAN%"=="1" if exist "build\%PRESET%" rmdir /s /q "build\%PRESET%"

echo [build] Configurando (%PRESET%)...
cmake --preset %PRESET% || exit /b 1
echo [build] Compilando...
cmake --build --preset %PRESET% || exit /b 1

if "%RUN_TESTS%"=="0" goto done
echo [build] Ejecutando pruebas...
ctest --preset %PRESET% || exit /b 1
:done

echo.
echo [build] Listo:
echo   build\%PRESET%\apps\cli\icpu.exe
echo   build\%PRESET%\apps\gui\icpu-gui.exe
echo   build\%PRESET%\tests\icpu_tests.exe
endlocal
