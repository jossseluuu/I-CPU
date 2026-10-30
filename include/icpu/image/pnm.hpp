#pragma once
// Familia Netpbm (PBM/PGM/PPM, variantes ASCII P1-P3 y binarias P4-P6).
// Formato sin compresion ideal para entender la estructura de una imagen.

#include "icpu/core/bytes.hpp"
#include "icpu/image/raster.hpp"

namespace icpu::image {

Raster pnm_decode(ByteSpan data);
// Escribe P5 (1 canal) o P6 (3 canales) binario, 8 o 16 bits.
Bytes pnm_encode(const Raster& raster);

}  // namespace icpu::image
