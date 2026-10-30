# 0001 · Compilar en Windows con MSVC

**Estado:** aceptada (2026-10-30)

## Contexto
El proyecto se empezó con MinGW-w64 (GCC 16). En el equipo de desarrollo está activado
**Smart App Control** de Windows 11, que bloquea ejecutables sin una firma o reputación
reconocida. Bloqueaba `cmake.exe`, `as.exe` y `ld.exe` de la distribución de MinGW
(error `0xC0E90002`, *STATUS_SYSTEM_INTEGRITY_POLICY_VIOLATION*), así que no se podía
compilar nada. Desactivar Smart App Control no es buena opción, porque no se puede volver a
activar sin reinstalar Windows.

## Decisión
En Windows se compila con **MSVC** desde *Build Tools para Visual Studio*, usando el CMake y el
Ninja que trae Visual Studio. Todo está firmado por Microsoft.

- Presets `msvc-release` / `msvc-debug` en `CMakePresets.json`.
- `scripts\build.bat` localiza Visual Studio con `vswhere`, carga `vcvars64.bat` y compila.

## Consecuencias
- Basta con instalar una cosa (Build Tools con "Desarrollo para el escritorio con C++").
- El código sigue siendo portable: los presets `release`/`debug` usan el compilador por defecto
  (GCC/Clang en Linux o macOS) y `CMakeLists.txt` mantiene la configuración de MinGW.
- MSVC exige `/permissive-` y `/W4`, y ha destapado un fallo real: un `setjmp` encadenado con
  `||` en `jpeg.cpp`, que era comportamiento indefinido.
