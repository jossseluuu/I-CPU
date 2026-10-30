#pragma once
// Imagen en memoria en el dominio espacial: matriz de muestras entrelazadas
// (R,G,B,A,R,G,B,A,...) de 8 o 16 bits. Se guarda siempre en uint16_t para
// tratar ambas profundidades con el mismo codigo.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace icpu::image {

struct Raster {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t channels = 0;  // 1 = gris, 2 = gris+alfa, 3 = RGB, 4 = RGBA
    std::uint8_t depth = 8;     // bits por muestra: 8 o 16
    std::vector<std::uint16_t> samples;

    Raster() = default;
    Raster(std::uint32_t w, std::uint32_t h, std::uint8_t ch, std::uint8_t d)
        : width(w), height(h), channels(ch), depth(d), samples(std::size_t(w) * h * ch, 0) {}

    std::size_t pixel_count() const { return std::size_t(width) * height; }
    bool has_alpha() const { return channels == 2 || channels == 4; }
    std::uint8_t color_channels() const { return has_alpha() ? channels - 1 : channels; }
    std::uint16_t max_value() const { return depth == 16 ? 0xFFFF : 0xFF; }

    std::uint16_t& at(std::size_t pixel, unsigned channel) { return samples[pixel * channels + channel]; }
    std::uint16_t at(std::size_t pixel, unsigned channel) const { return samples[pixel * channels + channel]; }
};

// Conversiones de forma (se registran como avisos porque alteran la imagen).
Raster convert_channels(const Raster& src, std::uint8_t channels);
Raster convert_depth(const Raster& src, std::uint8_t depth);
// Imagen RGB de 8 bits (para metricas, JPEG y visualizacion).
Raster to_rgb8(const Raster& src);

}  // namespace icpu::image
