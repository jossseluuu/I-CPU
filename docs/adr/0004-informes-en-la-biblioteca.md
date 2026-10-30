# 0004 · Análisis y experimentos en la biblioteca (`report.hpp`)

**Estado:** aceptada (2026-10-30)

## Contexto
Los comandos `analyze` y `bench` se escribieron primero dentro de la CLI. La GUI necesitaba
exactamente lo mismo, y duplicarlo suponía unas 100 líneas repetidas que podrían divergir.

## Decisión
La lógica pasa a `include/icpu/report.hpp` (`analyze()`, `bench()`, `load_pixels()` y los
veredictos). Se coloca junto a `icpu.hpp` y no en `analysis/`, porque `bench()` usa la API de
alto nivel (`embed`, `inspect`). `analysis/` es una capa inferior y no debe depender de ella.

## Consecuencias
- La CLI y la GUI solo formatean resultados.
- `report` tiene pruebas propias (`tests/test_analysis.cpp`, suite *report*).
- La biblioteca se puede usar sin ninguna interfaz para hacer experimentos propios.
