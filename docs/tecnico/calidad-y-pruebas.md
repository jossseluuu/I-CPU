# Calidad, pruebas y desarrollo

> **Para quién:** quien vaya a modificar I-CPU. Explica cómo se compila, qué se prueba, cómo se
> añade una prueba y qué comprueba la integración continua.

## Compilar

| Sistema | Orden | Resultado |
| --- | --- | --- |
| Windows | `scripts\build.bat [release\|debug] [test] [clean]` | `build\msvc-<config>\` |
| Linux / macOS | `cmake --preset release && cmake --build --preset release` | `build/release/` |
| Sanitizers | `cmake --preset sanitize && cmake --build --preset sanitize` | ASan + UBSan (GCC o Clang) |
| Fuzzing | ver [Fuzzing](#fuzzing) | Clang + libFuzzer |

Opciones de CMake: `ICPU_BUILD_CLI`, `ICPU_BUILD_GUI`, `ICPU_BUILD_TESTS` (por defecto `ON`),
`ICPU_SANITIZE` e `ICPU_BUILD_FUZZ` (por defecto `OFF`), e `ICPU_STATIC_RUNTIME` (MinGW). La versión del programa sale solo de
`project(... VERSION x.y.z)` en `CMakeLists.txt`.

Avisos: `/W4 /permissive- /utf-8` en MSVC y `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` en GCC/Clang (objetivo
`icpu_warnings`). En Windows los ejecutables llevan **Control Flow Guard** (`/guard:cf`), ASLR y DEP.

## Pruebas

68 casos con [doctest](https://github.com/doctest/doctest) en un único ejecutable,
`icpu_tests`, que se lanza con `ctest` o directamente:

```bat
scripts\build.bat test
build\msvc-release\tests\icpu_tests.exe -ts=stego        &rem solo una suite
build\msvc-release\tests\icpu_tests.exe -tc="*STC*"      &rem casos por nombre
```

| Suite | Fichero | Qué cubre |
| --- | --- | --- |
| `core` | `test_core.cpp` | bits, bytes, JSON, ficheros y rutas UTF-8 |
| `crypto` | `test_crypto.cpp` | vectores de prueba de Argon2id, BLAKE2b y XChaCha20-Poly1305; derivación de claves |
| `codecs` | `test_codecs.cpp` | ida y vuelta de PNG (8/16 bits), QOI, PNM, BMP, TGA, JPEG y GIF; metadatos |
| `stego` | `test_stego.cpp` | invariantes de las portadoras, ocultar → extraer en los tres dominios y todos los modos, imágenes golden v1/v2/v3 |
| `stc`, `formato` | `test_format.cpp` | STC (ida y vuelta, costes, segmentos), formato v3, sal aleatoria, ausencia del peine, simetría DCT, nombres hostiles |
| `analysis`, `report` | `test_analysis.cpp` | chi-cuadrado (también el barrido por ventanas), RS, chi-cuadrado DCT, métricas, informes |
| `robustez` | `test_robustness.cpp` | límites de memoria y dimensiones, zlib, ficheros mutados |
| `cli` | `test_core.cpp` | análisis de argumentos de la CLI (posicionales, valores, banderas) |
| `gui` | `test_gui.cpp` | servidor real: token, Host, cabeceras, API, streaming, presencia y apagado |

### Principios

- **Imágenes generadas en código** y deterministas ([D5](decisiones-de-diseno.md#d5-imágenes-de-prueba-generadas-en-código)),
  más una imagen real por formato y versión en `tests/golden/` para la compatibilidad.
- **Probar propiedades, no solo casos**: por ejemplo, que dos inserciones en la misma imagen no
  comparten posiciones, que el histograma DCT sigue simétrico o que STC cambia menos ranuras que
  escribir bit a bit.
- **Cada fallo corregido deja una prueba**: el peine de LSB matching, el GIF de 35 bytes que pedía
  1 GiB, los nombres `C:x`, el barrido que dejó de ver la demo secuencial…
- **Nada de esperas reales**: lo que depende del tiempo (presencia de pestañas) recibe el reloj
  como argumento.

### Añadir una prueba

```cpp
// tests/test_stego.cpp
TEST_CASE("Mi caso: ocultar y extraer un fichero vacio") {
    const icpu::Bytes cover = icpu::image::png_encode(test::natural_raster(64, 48));
    icpu::EmbedOptions opt;
    opt.password = "x";
    opt.kdf = {1024, 1, 1};  // Argon2id rapido en las pruebas
    const auto r = icpu::embed(cover, icpu::Payload::file("vacio.bin", {}), opt);
    const auto p = icpu::extract(r.image, {.password = "x", .kdf = opt.kdf});
    CHECK(p.name == "vacio.bin");
    CHECK(p.data.empty());
}
```

Usa `opt.kdf = {1024, 1, 1}` salvo que la prueba sea de la propia KDF: Argon2id real a 64 MiB
multiplicaría el tiempo de la suite.

## Fuzzing

`fuzz/fuzz_decoders.cpp` es un objetivo de libFuzzer que pasa datos arbitrarios por todo lo que
procesa una imagen de origen desconocido (decodificadores, `inspect`, `extract`). El primer byte
elige el formato. Requiere Clang:

```bash
cmake -S . -B build/fuzz -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DICPU_BUILD_FUZZ=ON -DICPU_SANITIZE=ON
cmake --build build/fuzz
build/fuzz/fuzz/icpu_fuzz_decoders -max_total_time=120 tests/golden
```

## Integración continua

`.github/workflows/ci.yml`, en cada push a `main` y en cada pull request:

| Trabajo | Sistema | Qué hace |
| --- | --- | --- |
| Windows (MSVC) | `windows-latest` | `scripts\build.bat release test` y publica `icpu.exe` e `icpu-gui.exe` como artefacto |
| Linux (GCC) | `ubuntu-latest` | preset `release` y pruebas |
| ASan + UBSan | `ubuntu-latest` | preset `sanitize` y pruebas |
| Fuzzing | `ubuntu-latest` | 2 minutos de libFuzzer sobre los decodificadores, con `tests/golden` como corpus |

## Documentación y figuras

Las figuras de los experimentos y las capturas de la GUI se generan con datos reales:

```bat
node scripts\generar_figuras.mjs              &rem gráficas SVG y figuras PNG (≈ 15 s)
node scripts\generar_figuras.mjs --capturas   &rem además, capturas de la GUI en ambos temas
```

Requiere Node.js 22 o posterior y Edge o Chrome; no usa paquetes de npm. Los diagramas de
`docs/img/diagramas/` son SVG hechos a mano: se editan directamente y llevan tema claro y oscuro
(`@media (prefers-color-scheme: dark)`).

## Estilo

- **C++20**, sin excepciones silenciadas: los errores son `icpu::Error` con código.
- Código, comentarios y textos de consola en **español sin tildes** (evita problemas de
  codificación en consolas antiguas). La web y la documentación, con tildes.
- Una responsabilidad por fichero; las interfaces no contienen lógica.
- Antes de cambiar el formato, lee [Decisiones de diseño](decisiones-de-diseno.md) y la
  [especificación](formato.md#8-compatibilidad-hacia-delante): casi cualquier cambio exige v4.

Cómo proponer cambios: [CONTRIBUTING.md](../../CONTRIBUTING.md).
