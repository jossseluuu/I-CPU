# Instalación

> **Para quién:** cualquiera que quiera usar I-CPU en su equipo. **Tiempo:** 10–20 minutos la
> primera vez (casi todo es la descarga de las herramientas de compilación).

I-CPU son dos programas que no necesitan instalación: `icpu.exe` (línea de comandos) e
`icpu-gui.exe` (interfaz gráfica). Se pueden obtener ya compilados o compilarlos a partir del código.

## Opción A: ejecutables ya compilados (Windows)

La integración continua compila y publica los ejecutables de Windows en cada cambio:

1. Abre el repositorio en GitHub → pestaña **Actions** → la última ejecución en verde de **CI**.
2. Descarga el artefacto **`icpu-windows-x64`** (un `.zip`) y descomprímelo en una carpeta.
3. Haz doble clic en `icpu-gui.exe`.

Si hay versiones publicadas en **Releases**, usa esas: son las estables.

> Los ejecutables no van firmados, así que Windows puede mostrar «Windows protegió su PC»
> (SmartScreen). Pulsa *Más información* → *Ejecutar de todas formas* solo si los has descargado del
> repositorio oficial. Ver [problemas frecuentes](#problemas-frecuentes).

## Opción B: compilar en Windows

### 1. Requisitos

Instala las **[Build Tools para Visual Studio](https://visualstudio.microsoft.com/es/downloads/)**
(gratuitas; sirve también Visual Studio Community) con la carga de trabajo
**«Desarrollo para el escritorio con C++»**. Incluye el compilador MSVC, CMake y Ninja: no hace
falta nada más. Todas las dependencias de I-CPU van dentro del repositorio.

### 2. Descargar el código

```bat
git clone https://github.com/jossseluuu/I-CPU.git
cd I-CPU
```

(o descarga el `.zip` desde GitHub con *Code → Download ZIP*).

### 3. Compilar y probar

```bat
scripts\build.bat test
```

El script busca Visual Studio, prepara el entorno, compila en modo optimizado y ejecuta las 68
pruebas. La primera vez tarda unos minutos (libjpeg-turbo es lo más pesado). Al final indica dónde
están los ejecutables:

```text
[build] Listo:
  build\msvc-release\apps\cli\icpu.exe
  build\msvc-release\apps\gui\icpu-gui.exe
  build\msvc-release\tests\icpu_tests.exe
```

Opciones de `build.bat`: `release` (por defecto), `debug`, `test` (ejecuta las pruebas) y `clean`
(borra la compilación anterior). Se pueden combinar: `scripts\build.bat debug clean test`.

### 4. Usarlo desde cualquier carpeta (opcional)

Copia `icpu.exe` e `icpu-gui.exe` a una carpeta, por ejemplo `C:\Herramientas\I-CPU`, y añádela al
`PATH`: *Configuración → Sistema → Información → Configuración avanzada del sistema → Variables de
entorno → Path → Nuevo*. Después, en una consola nueva, `icpu version` debe responder.

## Opción C: compilar en Linux o macOS

Requisitos: un compilador con C++20 (GCC 11+, Clang 14+ o Apple Clang 15+), CMake 3.20+ y Ninja.

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build git

git clone https://github.com/jossseluuu/I-CPU.git && cd I-CPU
cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/apps/gui/icpu-gui
```

La CI compila y prueba en Linux en cada cambio. macOS no se prueba de forma continua, pero el
código no depende de nada específico de Windows salvo donde está aislado (consola, mutex de instancia única).

## Comprobar que funciona

```bat
icpu version
icpu extract samples\demo_mensaje.png -p icpu-demo
```

Debe mostrar el mensaje de la demo:

```text
Hola! Si lees esto, has extraido correctamente el mensaje de la demo de I-CPU.
```

## Problemas frecuentes

| Síntoma | Causa | Solución |
| --- | --- | --- |
| `No se encuentra Visual Studio` | Faltan las Build Tools | Instálalas con «Desarrollo para el escritorio con C++». |
| `Visual Studio no tiene instalado el compilador de C++` | Falta la carga de trabajo | Abre *Visual Studio Installer → Modificar* y márcala. |
| Un `.exe` recién compilado no arranca, o las pruebas salen como *Not Run* | **Smart App Control** bloquea a veces binarios sin firma | Vuelve a compilar (cambia el hash): `scripts\build.bat clean test`. Ver [D1](../tecnico/decisiones-de-diseno.md#d1-compilar-en-windows-con-msvc). |
| «Windows protegió su PC» | SmartScreen con un ejecutable sin firma | *Más información → Ejecutar de todas formas*, si viene del repositorio oficial. |
| Con MinGW, `as.exe` o `ld.exe` fallan con `0xC0E90002` | Smart App Control bloquea MinGW | Usa MSVC (`build.bat`). |
| `icpu-gui` no abre el navegador | Sin navegador predeterminado, o `--no-browser` | Copia el enlace que muestra la consola (incluido `#t=…`). |
| `No se puede usar el puerto N` | Otro programa usa ese puerto | Quita `--port` para que el sistema elija uno libre. |
| La web dice «Falta el token de sesión» | Se abrió la dirección sin `#t=…` | Abre el enlace completo de la consola o vuelve a ejecutar `icpu-gui`. |

Siguiente paso: [Primeros pasos](primeros-pasos.md).
