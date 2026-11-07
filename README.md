# I-CPU

[![CI](https://github.com/jossseluuu/I-CPU/actions/workflows/ci.yml/badge.svg)](https://github.com/jossseluuu/I-CPU/actions/workflows/ci.yml)
[![Licencia: MIT](https://img.shields.io/badge/licencia-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-informational.svg)
![Plataformas](https://img.shields.io/badge/plataformas-Windows%20%7C%20Linux-lightgrey.svg)

Esteganografía **cifrada** en imágenes: oculta un texto o un fichero dentro de una imagen de
forma que, sin la contraseña, no se puede leer, y procura que sea difícil detectar que está ahí.
Ninguna herramienta puede garantizar lo segundo: ver [Qué protege y qué no](#qué-protege-y-qué-no).

- **Tres dominios de ocultación**, según el formato de salida:
  - *espacial* (bits bajos de los píxeles): PNG, BMP, TGA, QOI, PNM
  - *frecuencial* (coeficientes DCT): JPEG, sin recomprimir la imagen
  - *paleta* (índices de color, estilo EzStego): GIF, incluidos los animados
- **Inserción adaptativa** con Syndrome-Trellis Codes: cambia pocas muestras y en zonas con textura,
  con una carga limitada a un nivel prudente ([ADR 0008](docs/adr/0008-stc-adaptativo-por-defecto.md)).
- **Criptografía**: Argon2id (derivación de clave) + XChaCha20-Poly1305 (cifrado autenticado). Sal
  aleatoria en cada inserción, posiciones que salen de la contraseña y cabecera cifrada
  ([formato v3](docs/adr/0007-formato-v3-sal-almacenada.md)). Se siguen leyendo las imágenes v1 y v2.
- **Estegoanálisis incluido** (chi-cuadrado, análisis RS, chi-cuadrado DCT) y métricas de
  distorsión (PSNR, SSIM) para evaluar lo detectable que es cada configuración.
- Dos interfaces: **CLI** (`icpu.exe`) y **GUI web local** (`icpu-gui.exe`, un solo ejecutable).

## Inicio rápido (Windows)

Requisito: [Build Tools para Visual Studio](https://visualstudio.microsoft.com/es/downloads/)
con la carga de trabajo **"Desarrollo para el escritorio con C++"** (incluye compilador, CMake y Ninja).

```bat
scripts\build.bat test
build\msvc-release\apps\gui\icpu-gui.exe
```

`build.bat` acepta `release` (por defecto), `debug`, `test` (ejecuta las pruebas) y `clean`.
En otros sistemas: `cmake --preset release && cmake --build --preset release && ctest --preset release`.

> ¿Por qué MSVC y no MinGW? Smart App Control de Windows bloquea binarios sin firma
> reconocida, como el ensamblador y el enlazador de muchas distribuciones de MinGW.
> Ver [ADR 0001](docs/adr/0001-compilar-con-msvc.md).

## Uso

### Interfaz gráfica

`icpu-gui.exe` arranca un servidor **solo en 127.0.0.1** y abre el navegador. Tiene cuatro
pestañas: *Ocultar*, *Extraer*, *Analizar* y *Experimento*. Se cierra con el botón de apagado
(arriba a la derecha) o con Ctrl+C en la consola. Opciones: `--port N` y `--no-browser`.

- **Comparador** antes/después con deslizador, vista de diferencias y **lupa** de píxeles.
- **Medidor de capacidad** en vivo, medidor de fuerza y **generador de contraseñas**.
- Pega imágenes con `Ctrl+V` o suéltalas en cualquier parte de la ventana.
- **Historial** de operaciones solo en memoria: al recargar desaparece y no queda nada en el equipo.
- Experimento con **progreso en vivo** y gráficas interactivas exportables a PNG.
- Tema claro, oscuro o automático. Funciona **sin conexión**: no carga nada de Internet.
- Atajos: `Ctrl+Enter` ejecuta, `Alt+1…4` cambia de
  pestaña, `?` muestra todos.

### Línea de comandos

Ejecutado **sin argumentos** (o con doble clic), `icpu` entra en un **modo guiado** que pregunta
paso a paso qué hacer; puedes arrastrar las imágenes a la consola. Con argumentos:

```bat
icpu embed foto.png -o secreta.png -m "mensaje"         &rem modo adaptativo; pide la contraseña
icpu embed foto.jpg -o secreta.jpg -f documento.pdf     &rem oculta un fichero (JPEG: dominio DCT)
icpu embed foto.png -o secreta.png -f grande.zip --mode replace  &rem más capacidad, más detectable
icpu extract secreta.png                                &rem muestra el texto o guarda el fichero
icpu info foto.png                                      &rem formato y capacidad en cada dominio
icpu analyze sospechosa.png --cover original.png --export analisis\
icpu bench foto.png --csv resultados.csv                &rem experimento sistemático
icpu formats
```

El modo por defecto es `adaptive`: si el mensaje no cabe con una carga prudente, falla y lo explica
en lugar de subirla. `--mode match` (±1) y `--mode replace` (sustitución, hasta 4 bits por ranura)
admiten mucho más, a cambio de ser más detectables.

La contraseña se toma, por orden, de `--password-file F`, `-p CLAVE`, la variable
`ICPU_PASSWORD` o el teclado (sin eco). `icpu help` muestra la ayuda general con ejemplos, y
`icpu help <comando>` (o `icpu <comando> --help`) todas las opciones de un comando.

Códigos de salida: `0` correcto · `2` argumentos · `3` E/S · `4` formato · `5` capacidad ·
`6` autenticación (contraseña incorrecta, sin mensaje o imagen modificada) · `70` interno.

### Demos

`samples/` contiene una portadora sintética (`paisaje.png` y `paisaje.gif`) y varias imágenes con
mensaje. **Contraseña de las demos: `icpu-demo`**.

| Fichero | Contenido |
| --- | --- |
| `demo_mensaje.png` / `.jpg` / `.gif` | un texto, en cada uno de los tres dominios |
| `demo_secuencial.png` | un fichero en orden **secuencial** (`--layout sequential`): el estegoanálisis lo detecta |

```bat
icpu extract samples\demo_mensaje.jpg -p icpu-demo
icpu analyze samples\demo_secuencial.png --cover samples\paisaje.png
```

Se regeneran con `python scripts\generar_muestras.py` (portadoras) y `scripts\generar_demos.bat`.

### Experimentos

`experiments\run_bench.bat [imagen]` ejecuta `icpu bench` en los tres dominios y guarda los
CSV en `experiments\output\`, que git ignora.

## Estructura del repositorio

```text
include/icpu/   API pública de la biblioteca (icpu.hpp = alto nivel, report.hpp = análisis/experimentos)
src/             implementación de la biblioteca icpu_core
  core/          bytes, bits, errores, ficheros, JSON
  crypto/        Argon2id, XChaCha20-Poly1305, BLAKE2b, ChaCha20, aleatorio del SO
  compress/      DEFLATE (comprime el mensaje antes de cifrar)
  image/         modelo raster y códecs PNG, QOI, PNM (propios), BMP y TGA (stb)
  jpeg/          acceso a coeficientes DCT (libjpeg-turbo)
  gif/           códec GIF/LZW propio
  stego/         portadoras de cada dominio, orden de ranuras, carga útil y motor
  analysis/      métricas, estegoanálisis y visualizaciones
apps/cli/        icpu.exe
apps/gui/        icpu-gui.exe (servidor HTTP local + web/ embebida en el ejecutable)
tests/           pruebas doctest (imágenes generadas en código y golden/ con una real de cada formato)
fuzz/            objetivo de fuzzing de los decodificadores (Clang + libFuzzer)
third_party/     dependencias incluidas en el repositorio (versiones en VERSIONS.txt)
samples/         imágenes de ejemplo y demos
scripts/         compilación y generación de muestras
experiments/     script de experimentos (resultados en output/, ignorado)
docs/            arquitectura y decisiones de diseño (ADR)
```

## Documentación

- [docs/arquitectura.md](docs/arquitectura.md): cómo funciona por dentro (formato del contenedor, modos,
  dominios, seguridad, limitaciones).
- [docs/adr/](docs/adr/): decisiones de diseño y su porqué.

## Dependencias

Todas están incluidas en `third_party/` y se compilan con el proyecto. Las versiones exactas
están en [third_party/VERSIONS.txt](third_party/VERSIONS.txt).

| Biblioteca | Uso | Licencia |
| --- | --- | --- |
| Monocypher 4 | Argon2id, XChaCha20-Poly1305, BLAKE2b | BSD-2 / CC0 |
| libjpeg-turbo 3 | coeficientes DCT de JPEG | BSD-3 / IJG |
| stb_image / stb_image_write | lectura de formatos y BMP/TGA | MIT / dominio público |
| cpp-httplib | servidor de la GUI | MIT |
| doctest | pruebas | MIT |

## Qué protege y qué no

- **Contenido:** sin la contraseña no se puede leer ni alterar sin que se note. Depende de una
  contraseña fuerte: usa el generador de la GUI.
- **Existencia:** el modo adaptativo con poca carga resiste bien los ataques clásicos, pero **no
  es indetectable**. Un detector moderno puede encontrar cargas altas, y no se ha evaluado frente a
  ellos. Quien tenga la imagen original puede compararla: no reutilices ni publiques la portadora.
- **Durabilidad:** ninguna. Recomprimir, redimensionar o editar la imagen destruye el mensaje, y las
  redes sociales y la mensajería suelen recomprimir.

I-CPU es un proyecto personal de aprendizaje. Usa primitivas criptográficas sólidas, pero
**no ha sido auditado**.

Si encuentras un problema de seguridad, sigue [SECURITY.md](SECURITY.md) y no abras un issue público.

## Licencia

[MIT](LICENSE). Las bibliotecas de `third_party/` conservan sus propias licencias (ver
[Dependencias](#dependencias)). Los cambios entre versiones están en el [CHANGELOG](CHANGELOG.md).
