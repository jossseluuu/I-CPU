#pragma once
// Codec GIF (GIF87a/GIF89a) propio, implementado desde cero.
//
// Un GIF no guarda colores sino INDICES a una paleta de hasta 256 colores,
// comprimidos con LZW. Para ocultar datos hay que trabajar sobre esos
// indices, por lo que se necesita acceso a ellos (stb solo devuelve RGBA).
//
// Se conserva todo el fichero: cabecera, paletas, extensiones (retardos de
// animacion, transparencia, bucle NETSCAPE, comentarios) y todos los
// fotogramas. Solo se recodifican los datos LZW de los indices.

#include <cstdint>
#include <vector>

#include "icpu/core/bytes.hpp"
#include "icpu/image/raster.hpp"

namespace icpu::gif {

struct Frame {
    Bytes extensions_before;  // bloques de extension en bruto que preceden al fotograma
    std::uint16_t left = 0, top = 0, width = 0, height = 0;
    std::uint8_t packed = 0;  // campo empaquetado del descriptor de imagen
    Bytes local_palette;      // RGB, vacio si usa la global
    int transparent_index = -1;
    std::uint8_t min_code_size = 8;
    std::vector<std::uint8_t> indices;  // width * height, en el orden almacenado

    const Bytes& palette(const Bytes& global) const { return local_palette.empty() ? global : local_palette; }
};

struct Image {
    Bytes header;          // "GIF89a" + descriptor logico de pantalla (13 bytes)
    Bytes global_palette;  // RGB
    std::vector<Frame> frames;
    Bytes trailing_blocks;  // extensiones tras el ultimo fotograma

    std::uint16_t width() const { return get_u16le(header.data() + 6); }
    std::uint16_t height() const { return get_u16le(header.data() + 8); }
};

Image decode(ByteSpan file);
Bytes encode(const Image& image);

// Compresion LZW de GIF (expuesta para las pruebas).
std::vector<std::uint8_t> lzw_decode(ByteSpan data, std::uint8_t min_code_size, std::size_t expected);
Bytes lzw_encode(const std::vector<std::uint8_t>& indices, std::uint8_t min_code_size);

// Primer fotograma compuesto sobre el lienzo, como RGBA de 8 bits (para analisis).
image::Raster first_frame_rgba(const Image& image);

}  // namespace icpu::gif
