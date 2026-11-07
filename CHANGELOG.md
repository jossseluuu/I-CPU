# Registro de cambios

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/).
Versiones según [Versionado Semántico](https://semver.org/lang/es/).

## [2.0.0] - 2026-10-31

Versión centrada en la indetectabilidad: nuevo formato v3, inserción adaptativa por defecto y
corrección de los fallos encontrados en una revisión de la 1.5.

### Añadido

- **Modo adaptativo** (`--mode adaptive`, por defecto): Syndrome-Trellis Codes con costes HILL
  (espacial), UERD (JPEG) y distancia de color (GIF). Ver [ADR 0008](docs/adr/0008-stc-adaptativo-por-defecto.md).
- **Formato v3** con sal aleatoria guardada en la imagen. Ver [ADR 0007](docs/adr/0007-formato-v3-sal-almacenada.md).
- La sal y la cabecera también van con STC, en todos los modos, para no dejar cambios aislados en
  zonas lisas.
- Aviso si la imagen es casi toda lisa, como una captura de pantalla, o si un PNG animado va a
  perder fotogramas.
- `icpu info` y la GUI muestran la capacidad en modo adaptativo; el experimento lo incluye.
- Imágenes de referencia reales de cada formato (`tests/golden`), pruebas con ficheros mutados,
  objetivo de fuzzing (`fuzz/`) y trabajos de CI con ASan, UBSan y libFuzzer.
- Control Flow Guard en los ejecutables de Windows.

### Cambiado

- **Incompatible:** `embed` usa el modo adaptativo y, si el mensaje no cabe con una carga prudente,
  falla en lugar de subirla. Para la capacidad de antes, usa `--mode replace`.
- **Incompatible:** `EmbedOptions::mode` es `Adaptive` por defecto y se elimina
  `EngineParams::format_version`: v1 y v2 ya no se pueden escribir.
- Las contraseñas se normalizan a Unicode NFC en Windows y en la GUI.
- Una contraseña incorrecta tarda el doble, porque se prueban las sales nueva y antigua.
- JPEG necesita al menos 368 bloques, unos 130×130 px en 4:2:0, para la sal y la cabecera.

### Corregido

- **LSB matching** dejaba un peine de periodo 16 en el histograma, fácil de detectar.
- **Posiciones repetidas:** la misma imagen y contraseña usaban siempre las mismas posiciones.
- **JPEG:** el esquema tipo JSteg rompía la simetría del histograma de coeficientes.
- **Consumo de memoria:** un GIF de 35 bytes podía reservar casi 1 GiB. Hay límites de dimensiones
  en todos los formatos y la descompresión de la carga útil tiene un máximo.
- **`ICPU_PASSWORD`** no se leía como UTF-8 en Windows: una contraseña con ñ o tildes daba otra clave.
  Un mensaje guardado así con versiones anteriores no se abrirá con la 2.0.
- **Nombres de fichero hostiles:** un nombre como `C:x` podía escribir fuera de la carpeta elegida al
  extraer. Se neutralizan también `x:y` (flujos de NTFS), los caracteres de control y dispositivos
  como `NUL`.
- La salida en terminal de `extract` ya no ejecuta secuencias de escape del mensaje.
- Una falta de memoria durante STC ya no cierra el programa de golpe.
- La GUI y el modo guiado ya no añaden `_icpu` al nombre de los ficheros, y la GUI no guarda el
  historial en el navegador.
- Se aceptan ficheros de contraseña con BOM.
- El README prometía que no se podía demostrar que el mensaje existe; ahora explica el alcance real.

## [1.5.0] - 2026-10-30

Interfaz web rediseñada, formato oculto v2 con la cabecera cifrada y una CLI más amable.

### Añadido

- **Formato v2**: la cabecera (versión, parámetros y longitud) va cifrada con XChaCha20 y una
  clave propia. En la imagen ya no queda nada reconocible, ni con orden secuencial
  ([ADR 0006](docs/adr/0006-formato-v2-cabecera-cifrada.md)).
- **Interfaz web nueva**, minimalista, con tema claro, oscuro o automático y 100 % offline:
  - comparador antes/después con deslizador, vista de diferencias ×64 y **lupa de píxeles**
    que muestra coordenadas y color;
  - medidor de capacidad en vivo mientras escribes, medidor de fuerza de contraseña y
    **generador de contraseñas** seguras;
  - pegar imágenes con Ctrl+V, soltar ficheros en cualquier parte de la ventana y vistas
    previas de QOI, TGA, PNM, PSD…;
  - **historial** de operaciones (solo metadatos) y atajos de teclado (Ctrl+Enter, Alt+1…4, Alt+H, ?);
  - experimento con **barra de progreso en vivo** y tiempo restante; gráficas interactivas
    (leyenda para ocultar series, tooltip, exportar a PNG) y curva de SSIM;
  - PSNR/SSIM y avisos de envío ("no la mandes por WhatsApp") en el resultado.
- API del servidor: `POST /api/preview`; `/api/embed` devuelve calidad, mapa de diferencias y
  vistas previas; `/api/bench` transmite el progreso (NDJSON).
- CLI: **modo guiado** al ejecutar `icpu` sin argumentos (o `icpu guided`), ayuda con ejemplos
  y ayuda detallada por comando (`icpu help embed`, `icpu embed --help`).
- Pruebas del servidor HTTP (seguridad, ocultar→extraer, vista previa y streaming) y del
  formato v2 (compatibilidad con v1, cabecera irreconocible, manipulación). 46 casos en total.

### Cambiado

- Las imágenes nuevas se escriben en v2. **Las de la 1.0 (v1) se siguen pudiendo extraer.**
- La versión del programa sale de `CMakeLists.txt` (una sola fuente).
- El servidor de la GUI es una biblioteca (`icpu_gui_server`) para poder probarlo.

### Corregido

- El modo guiado y `read_line` ignoran la marca BOM UTF-8 que PowerShell antepone al texto
  que se envía por tubería.
- Comentario dañado en `keystream.cpp` por el renombrado Umbra → I-CPU.
- Enlace con Threads en Linux para cpp-httplib.

## [1.0.0] - 2026-10-30

Primera versión publicada.

### Añadido

- Biblioteca `icpu_core`: esteganografía cifrada en tres dominios.
  - **Espacial** (LSB de 1 a 4 bits): PNG (8 y 16 bits, conserva metadatos), BMP, TGA, QOI y PNM.
  - **Frecuencial** (coeficientes DCT): JPEG baseline, progresivo y aritmético, sin recomprimir.
  - **Paleta** (EzStego): GIF, incluidos los animados.
- Criptografía: Argon2id (64 MiB, 3 pasadas), XChaCha20-Poly1305 y orden de escritura secreto
  derivado de la contraseña. Formato del contenedor `icpu/v1`.
- Modos de escritura: sustitución LSB y LSB matching (±1); orden aleatorio o secuencial.
- Estegoanálisis: chi-cuadrado (y su barrido por porciones), análisis RS y chi-cuadrado DCT.
  Métricas MSE, PSNR y SSIM; planos de bits, mapa de diferencias e histogramas.
- CLI `icpu`: `embed`, `extract`, `info`, `analyze`, `bench`, `formats` y `version`, con salida `--json`.
- GUI `icpu-gui`: aplicación web local (solo 127.0.0.1, con token de sesión) en un único
  ejecutable, con pestañas Ocultar, Extraer, Analizar y Experimento.
- 38 casos de prueba (doctest), con imágenes generadas en código.
- Scripts de compilación con MSVC, generación de muestras y experimentos; imágenes de demostración.
- Documentación: README, arquitectura y registro de decisiones (ADR 0001–0005).

### Limitaciones conocidas

- La cabecera del contenedor (versión, parámetros y longitud) no está cifrada; solo la protege
  que sus posiciones son secretas. Ver [docs/arquitectura.md](docs/arquitectura.md#8-limitaciones-conocidas).

[2.0.0]: https://github.com/jossseluuu/I-CPU/compare/v1.5.0...v2.0.0
[1.5.0]: https://github.com/jossseluuu/I-CPU/compare/v1.0.0...v1.5.0
[1.0.0]: https://github.com/jossseluuu/I-CPU/releases/tag/v1.0.0
