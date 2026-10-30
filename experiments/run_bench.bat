@echo off
rem ===========================================================================
rem  Experimentos de distorsion y detectabilidad con "icpu bench".
rem  Los resultados (CSV) van a experiments\output\ (ignorado por git).
rem
rem  Uso:  experiments\run_bench.bat [imagen]
rem    Sin argumento usa la portadora de ejemplo samples\paisaje.png.
rem ===========================================================================
setlocal
cd /d "%~dp0.."
set "ICPU=build\msvc-release\apps\cli\icpu.exe"
if not exist "%ICPU%" (
    echo [bench] No existe %ICPU%. Compila primero con scripts\build.bat
    exit /b 1
)
set "COVER=%~1"
if "%COVER%"=="" set "COVER=samples\paisaje.png"
if not exist "experiments\output" mkdir "experiments\output"
set "NAME=%~n1"
if "%NAME%"=="" set "NAME=paisaje"

echo [bench] 1/3 Dominio espacial: k = 1..4, sustitucion vs matching, aleatorio vs secuencial
"%ICPU%" bench "%COVER%" --format png --bits 1,2,3,4 --fill 0.05,0.1,0.25,0.5,0.75,1 --csv "experiments\output\%NAME%_espacial.csv" || exit /b 1

echo.
echo [bench] 2/3 Dominio DCT (salida JPEG)
"%ICPU%" bench "%COVER%" --format jpg --bits 1 --modes replace --fill 0.05,0.1,0.25,0.5,0.75,1 --csv "experiments\output\%NAME%_dct.csv" || exit /b 1

rem El dominio de paleta necesita una portadora GIF: solo con la de ejemplo
if not "%~1"=="" goto done
if not exist "samples\paisaje.gif" goto done
echo.
echo [bench] 3/3 Dominio de paleta, GIF
"%ICPU%" bench samples\paisaje.gif --bits 1 --modes replace --fill 0.05,0.1,0.25,0.5,0.75,1 --csv "experiments\output\%NAME%_paleta.csv" || exit /b 1

:done
echo.
echo [bench] CSV en experiments\output\
endlocal
