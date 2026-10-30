#pragma once
// Catalogo de formatos de imagen soportados y su deteccion por "numero magico".
//
//  Formato | Lectura | Escritura | Dominio de ocultacion | Compresion
//  --------+---------+-----------+-----------------------+---------------------------
//  PNG     |   si    |    si     | espacial (LSB)        | sin perdida (DEFLATE)
//  BMP     |   si    |    si     | espacial (LSB)        | ninguna
//  TGA     |   si    |    si     | espacial (LSB)        | sin perdida (RLE)
//  PNM     |   si    |    si     | espacial (LSB)        | ninguna (PBM/PGM/PPM)
//  QOI     |   si    |    si     | espacial (LSB)        | sin perdida (QOI)
//  JPEG    |   si    |    si     | frecuencial (DCT)     | CON perdida
//  GIF     |   si    |    si     | paleta (indices)      | sin perdida (LZW), 256 colores
//  PSD/HDR/PIC | si  |    no     | -> se guarda como PNG |

#include <string_view>

#include "icpu/core/bytes.hpp"

namespace icpu::image {

enum class ImageFormat { Unknown, Png, Jpeg, Gif, Bmp, Tga, Pnm, Qoi, Psd, Hdr, Pic };

enum class Domain { Spatial, Dct, Palette };

ImageFormat detect_format(ByteSpan data, std::string_view extension_hint = {});
ImageFormat format_from_extension(std::string_view extension);

const char* format_name(ImageFormat format);
const char* default_extension(ImageFormat format);
const char* domain_name(Domain domain);

bool can_write(ImageFormat format);
bool is_lossy(ImageFormat format);
// Dominio en el que se oculta la informacion al escribir en ese formato.
Domain domain_for_output(ImageFormat format);

}  // namespace icpu::image
