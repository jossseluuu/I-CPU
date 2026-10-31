# Registro de cambios

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/).
Versiones según [Versionado Semántico](https://semver.org/lang/es/).

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

[1.5.0]: https://github.com/TU_USUARIO/I-CPU/compare/v1.0.0...v1.5.0
[1.0.0]: https://github.com/TU_USUARIO/I-CPU/releases/tag/v1.0.0
