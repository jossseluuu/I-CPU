#pragma once
// Codificador PNG propio (RFC 2083 / ISO 15948) y lector de chunks.
//
// Se implementa el codificador en lugar de usar stb_image_write para:
//   - conservar la profundidad de 16 bits (stb solo escribe 8 bits),
//   - reinsertar los chunks auxiliares del original (perfil ICC, gamma,
//     texto, EXIF, resolucion...): la imagen estego "conserva todo",
//   - elegir el filtro por fila (heuristica de minima suma absoluta).

#include <string>
#include <vector>

#include "icpu/core/bytes.hpp"
#include "icpu/image/raster.hpp"

namespace icpu::image {

struct PngChunk {
    std::string type;  // 4 caracteres ASCII
    Bytes data;
};

struct PngMetadata {
    std::uint8_t color_type = 0;
    std::uint8_t bit_depth = 0;
    std::vector<PngChunk> ancillary;  // todos los chunks auxiliares, en orden
};

std::vector<PngChunk> png_read_chunks(ByteSpan file);
PngMetadata png_read_metadata(ByteSpan file);

// Chunks del original que siguen siendo validos para la imagen de salida.
std::vector<PngChunk> png_chunks_to_preserve(const PngMetadata& source, const Raster& output);

Bytes png_encode(const Raster& raster, const std::vector<PngChunk>& extra_chunks = {});

std::uint32_t crc32(ByteSpan data, std::uint32_t crc = 0);

}  // namespace icpu::image
