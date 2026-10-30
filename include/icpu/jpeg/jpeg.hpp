#pragma once
// Acceso a JPEG a nivel de COEFICIENTES DCT, usando libjpeg-turbo.
//
// Por que hace falta: JPEG es con perdida. Si se ocultan bits en los pixeles
// y luego se comprime, la cuantizacion los destruye. La solucion es trabajar
// DESPUES de la cuantizacion: los coeficientes DCT cuantizados se codifican
// por entropia (Huffman o aritmetica), que es SIN perdida, asi que cualquier
// cambio en ellos se conserva exactamente al guardar y volver a leer.
//
//   pixeles -> YCbCr -> bloques 8x8 -> DCT -> cuantizacion -> [OCULTACION] -> entropia -> .jpg
//
// Es el mismo mecanismo que usa la herramienta "jpegtran" para hacer
// transformaciones sin perdida: se leen los coeficientes, se modifican y se
// vuelven a escribir sin pasar nunca por los pixeles. Soporta JPEG baseline,
// progresivo y aritmetico, con cualquier submuestreo de croma.

#include <array>
#include <cstdint>
#include <vector>

#include "icpu/core/bytes.hpp"
#include "icpu/image/raster.hpp"

namespace icpu::jpeg {

struct Component {
    int id = 0;
    int h = 1, v = 1;               // factores de muestreo
    std::uint32_t blocks_w = 0;     // bloques 8x8 por fila
    std::uint32_t blocks_h = 0;     // filas de bloques
    std::array<std::uint16_t, 64> qtable{};  // tabla de cuantizacion (orden natural)
    std::vector<std::int16_t> coefs;         // blocks_w * blocks_h * 64, orden natural (DC en [0])
};

struct CoefImage {
    std::uint32_t width = 0, height = 0;
    bool progressive = false;
    bool arithmetic = false;
    std::vector<Component> components;
};

// Lee los coeficientes cuantizados de cualquier JPEG soportado por libjpeg-turbo.
CoefImage read_coefficients(ByteSpan jpeg_file);

// Reescribe `original` sustituyendo sus coeficientes por los de `modified`.
// Conserva tablas de cuantizacion, submuestreo, modo (progresivo/aritmetico)
// y marcadores de metadatos (EXIF, ICC, XMP, comentarios...). Las tablas
// Huffman se optimizan para los nuevos datos.
Bytes write_coefficients(ByteSpan original, const CoefImage& modified);

// Compresion de pixeles (8 bits, 1 o 3 canales) con calidad IJG 1..100.
Bytes compress_pixels(const image::Raster& raster, int quality);

// Descompresion completa a pixeles (gris o RGB de 8 bits).
image::Raster decompress_pixels(ByteSpan jpeg_file);

// Calidad IJG aproximada a partir de la tabla de luminancia.
int estimate_quality(const CoefImage& image);

}  // namespace icpu::jpeg
