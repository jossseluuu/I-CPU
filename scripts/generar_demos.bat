@echo off
rem ===========================================================================
rem  Crea en samples\ las imagenes de demostracion CON mensaje oculto, a partir
rem  de las portadoras (scripts\generar_muestras.py). Contrasena: icpu-demo
rem
rem  Uso:  scripts\generar_demos.bat      (requiere haber compilado: scripts\build.bat)
rem ===========================================================================
setlocal
cd /d "%~dp0.."
set "ICPU=build\msvc-release\apps\cli\icpu.exe"
if not exist "%ICPU%" (
    echo [demos] No existe %ICPU%. Compila primero con scripts\build.bat
    exit /b 1
)
set "ICPU_PASSWORD=icpu-demo"
set "MSG=Hola! Si lees esto, has extraido correctamente el mensaje de la demo de I-CPU."

echo [demos] PNG (espacial, orden aleatorio)
"%ICPU%" embed samples\paisaje.png -o samples\demo_mensaje.png -m "%MSG%" --force >nul || exit /b 1
echo [demos] JPEG (coeficientes DCT)
"%ICPU%" embed samples\paisaje.png -o samples\demo_mensaje.jpg -m "%MSG%" --force >nul || exit /b 1
echo [demos] GIF (paleta, estilo EzStego)
"%ICPU%" embed samples\paisaje.gif -o samples\demo_mensaje.gif -m "%MSG%" --force >nul || exit /b 1
echo [demos] PNG con un fichero oculto en orden SECUENCIAL (para ver el estegoanalisis)
"%ICPU%" embed samples\paisaje.png -o samples\demo_secuencial.png -f scripts\generar_muestras.py --no-compress --mode replace --layout sequential --force >nul 2>nul || exit /b 1

echo.
echo [demos] Listo. Prueba:
echo   %ICPU% extract samples\demo_mensaje.png -p icpu-demo
echo   %ICPU% analyze samples\demo_secuencial.png --cover samples\paisaje.png
endlocal
