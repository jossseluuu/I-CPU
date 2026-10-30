# Registro de cambios

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/).
Versiones según [Versionado Semántico](https://semver.org/lang/es/).

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

[1.0.0]: https://github.com/TU_USUARIO/I-CPU/releases/tag/v1.0.0
