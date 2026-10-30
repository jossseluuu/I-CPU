#pragma once
// "Quite OK Image Format" (https://qoiformat.org/qoi-specification.pdf):
// formato sin perdida moderno y muy simple, implementado desde cero.

#include "icpu/core/bytes.hpp"
#include "icpu/image/raster.hpp"

namespace icpu::image {

Raster qoi_decode(ByteSpan data);
Bytes qoi_encode(const Raster& raster);  // requiere 3 o 4 canales de 8 bits

}  // namespace icpu::image
