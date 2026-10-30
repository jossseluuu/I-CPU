#pragma once
// Lectura/escritura de imagenes en el dominio espacial para cualquier formato.
//
// "Canonicalizar" es adaptar una imagen a lo que el formato de destino puede
// representar EXACTAMENTE (p. ej. BMP no tiene gris de 1 canal, QOI no tiene
// 16 bits). Es imprescindible: si la imagen que se escribe no se relee identica,
// el receptor veria otras ranuras y el mensaje seria irrecuperable.

#include <string>
#include <vector>

#include "icpu/core/bytes.hpp"
#include "icpu/image/format.hpp"
#include "icpu/image/png.hpp"
#include "icpu/image/raster.hpp"

namespace icpu::image {

// Decodifica cualquier formato legible (en GIF/APNG solo el primer fotograma).
Raster decode_raster(ByteSpan data, ImageFormat format);

// Codifica en un formato espacial sin perdida (PNG, BMP, TGA, PNM, QOI).
Bytes encode_raster(const Raster& raster, ImageFormat format, const std::vector<PngChunk>& png_extra = {});

// Adapta canales/profundidad al formato; anade a `notes` una explicacion de cada cambio.
Raster canonicalize_for(const Raster& raster, ImageFormat format, std::vector<std::string>& notes);

}  // namespace icpu::image
