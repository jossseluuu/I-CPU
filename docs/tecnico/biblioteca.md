# Usar I-CPU como biblioteca (C++)

> **Para quién:** desarrolladores que quieran integrar la esteganografía de I-CPU en su propio
> programa o hacer experimentos sin pasar por la CLI.

Todo lo que hacen `icpu.exe` e `icpu-gui.exe` está en la biblioteca estática **`icpu_core`**
(C++20). La API de alto nivel cabe en dos cabeceras:

| Cabecera | Funciones |
| --- | --- |
| [`include/icpu/icpu.hpp`](../../include/icpu/icpu.hpp) | `embed`, `extract`, `inspect`, `version` |
| [`include/icpu/report.hpp`](../../include/icpu/report.hpp) | `analyze`, `bench`, `load_pixels`, veredictos |

## Integración con CMake

La forma más sencilla es añadir el repositorio como subdirectorio:

```cmake
add_subdirectory(ruta/a/I-CPU EXCLUDE_FROM_ALL)
add_executable(mi_programa main.cpp)
target_link_libraries(mi_programa PRIVATE icpu_core)
target_compile_features(mi_programa PRIVATE cxx_std_20)
```

Las dependencias (Monocypher, libjpeg-turbo, stb) se compilan con el proyecto desde `third_party/`.

## Ocultar y extraer

```cpp
#include <icpu/icpu.hpp>
#include <icpu/core/file.hpp>

int main() {
    const icpu::Bytes cover = icpu::read_file("foto.png");

    icpu::EmbedOptions opt;
    opt.password = "correcto-caballo-bateria-grapa";
    // opt.mode = icpu::EmbedMode::Adaptive;   // por defecto
    // opt.output_format = icpu::image::ImageFormat::Jpeg;

    const icpu::EmbedResult r = icpu::embed(cover, icpu::Payload::text("Nos vemos a las 8"), opt, "foto.png");
    icpu::write_file("secreta.png", r.image);

    const icpu::Payload p = icpu::extract(r.image, {.password = opt.password});
    // p.kind == icpu::Payload::Kind::Text, p.as_text() == "Nos vemos a las 8"
}
```

Para un fichero: `icpu::Payload::file("contrato.pdf", bytes)`. Al extraer, `p.name` trae el nombre
original (ya sin rutas).

### `EmbedOptions`

| Campo | Por defecto | Significado |
| --- | --- | --- |
| `password` | — | Contraseña (UTF-8). Normalízala a NFC si viene del usuario. |
| `mode` | `Adaptive` | `Adaptive`, `Match` o `Replace` ([modos](esteganografia.md#3-modos-de-escritura)) |
| `bits_per_slot` | `0` (auto) | Solo `Replace`: 1–4, o 0 para el mínimo con el que cabe |
| `layout` | `Random` | `Sequential` solo para experimentos |
| `output_format` | el de la portadora | PNG, BMP, TGA, QOI, PNM, JPEG o GIF |
| `jpeg_quality` | `90` | Solo si hay que convertir a JPEG una portadora que no lo es |
| `compress` | `true` | DEFLATE antes de cifrar, si reduce |
| `verify` | `true` | Releer la imagen y extraer para comprobar |
| `kdf` | 64 MiB, 3 pasadas | No lo cambies: el receptor usa los valores fijos del formato |

### `EmbedResult`

`image` (bytes del fichero), `format`, `domain`, `bits_per_slot`, `message_bytes`, `packed_bytes`,
`compressed`, `capacity_bytes`, `usable_slots`, `stats` (ranuras usadas, cambiadas, bits),
`verified` y `notes` (avisos legibles: conversiones, imagen casi lisa, fotogramas perdidos…).

## Inspeccionar una imagen

```cpp
const icpu::ImageInfo info = icpu::inspect(icpu::read_file("foto.jpg"), "foto.jpg");
for (const auto& c : info.capacities)
    std::printf("%s k=%u: %zu B (adaptativo %zu B)\n",
                icpu::image::format_name(c.output), c.bits_per_slot, c.message_bytes, c.adaptive_bytes);
```

## Analizar y experimentar

```cpp
#include <icpu/report.hpp>

const icpu::AnalysisReport a = icpu::analyze(icpu::read_file("sospechosa.png"));
if (a.chi_square) std::printf("chi2 p = %.3f (%s)\n", a.chi_square->p_value, icpu::chi_square_verdict(a.chi_square->p_value));
if (a.rs)         std::printf("RS = %.3f (%s)\n", a.rs->estimated_rate, icpu::rs_verdict(a.rs->estimated_rate));
if (a.chi_square_sweep)
    for (std::size_t i = 0; i < a.chi_square_sweep->fraction.size(); ++i)
        std::printf("%3.0f %%  acumulado %.2f  ventana %.2f\n", a.chi_square_sweep->fraction[i] * 100,
                    a.chi_square_sweep->p_value[i], a.chi_square_sweep->window_p[i]);

icpu::BenchOptions b;          // llenados 5–100 %, k = 1 y 2, los tres modos, ambos órdenes
b.layouts = {icpu::Layout::Random};
const icpu::BenchReport rep = icpu::bench(icpu::read_file("foto.png"), b, "foto.png",
    [](std::size_t done, std::size_t total) { std::printf("%zu/%zu\r", done, total); });
for (const auto& run : rep.runs)
    std::printf("%s %.0f%%: PSNR %.1f dB, RS %.3f\n", icpu::mode_name(run.mode), run.fill * 100, run.psnr, run.detection.rs_rate);
```

`bench` usa un Argon2id reducido (1 MiB, 1 pasada): el experimento mide la esteganografía, no la KDF.

## Errores

Todas las funciones lanzan `icpu::Error` (deriva de `std::runtime_error`) con un `code()`:

```cpp
try {
    icpu::extract(imagen, {.password = clave});
} catch (const icpu::Error& e) {
    if (e.code() == icpu::ErrorCode::Authentication) { /* contraseña mal, sin mensaje o alterada */ }
    else if (e.code() == icpu::ErrorCode::Capacity) { /* el mensaje no cabe */ }
}
```

Ver la [tabla de errores](arquitectura.md#5-errores).

## Hilos

Las funciones no comparten estado global: se pueden llamar a la vez desde varios hilos con datos
distintos (la GUI lo hace). STC ya reparte su trabajo en varios hilos internamente.

## Estabilidad de la API

I-CPU sigue el [versionado semántico](https://semver.org/lang/es/): un cambio incompatible en
`icpu.hpp` o `report.hpp` sube la versión mayor. Las cabeceras de `include/icpu/<módulo>/` son
públicas pero de más bajo nivel, y pueden cambiar en versiones menores.
