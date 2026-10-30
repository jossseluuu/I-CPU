# 0005 · Imágenes de prueba generadas en código

**Estado:** aceptada (2026-10-30)

## Contexto
Las pruebas necesitan imágenes de muchos tipos: 8/16 bits, 1–4 canales, JPEG en gris y en color,
GIF, tamaños impares. Guardarlas como binarios en el repositorio pesa, cuesta revisarlas y no
cubre todas las combinaciones.

## Decisión
`tests/helpers.hpp` genera imágenes deterministas: ondas suaves más un ruido leve, que imitan una
foto. Usa SplitMix64 y no `std::*_distribution`, cuya salida cambia entre compiladores. Los JPEG y
GIF se crean con los propios códecs.

## Consecuencias
- Sin ficheros binarios en `tests/` (la carpeta `tests/data` se eliminó).
- Con un LCG simple, los bits bajos tienen periodos cortos y el estegoanálisis daba resultados
  absurdos. De ahí SplitMix64.
- Como las imágenes sintéticas son suaves, tienen pocos coeficientes AC útiles en JPEG. Las
  pruebas de JPEG usan imágenes de 256×192.
- Las imágenes de **demostración** para el usuario sí son ficheros (`samples/`), generados por
  `scripts/generar_muestras.py` y `scripts/generar_demos.bat`.
