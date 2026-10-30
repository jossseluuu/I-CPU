#pragma once
// Utilidades comunes de las pruebas: imagenes sinteticas deterministas y
// comprobacion de codigos de error.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include "icpu/core/bytes.hpp"
#include "icpu/core/error.hpp"
#include "icpu/crypto/kdf.hpp"
#include "icpu/gif/gif.hpp"
#include "icpu/image/raster.hpp"

namespace test {

// SplitMix64: determinista en cualquier compilador (std::*_distribution no lo es)
// y con bits bajos de calidad (un LCG simple tiene periodos cortos en ellos).
struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed) {}
    std::uint32_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
    }
    // Ruido triangular en [-1, 1].
    double noise() { return (double(next() % 2001) + double(next() % 2001) - 2000.0) / 2000.0; }
};

// Imagen "natural": ondas suaves por canal + ruido leve, como una foto. El
// canal alfa (si lo hay) es un degradado, para comprobar que no se toca.
inline icpu::image::Raster natural_raster(std::uint32_t w, std::uint32_t h, std::uint8_t channels = 3,
                                           std::uint8_t depth = 8, std::uint32_t seed = 1) {
    icpu::image::Raster r(w, h, channels, depth);
    Rng rng(seed);
    const double maxv = r.max_value();
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
            for (unsigned c = 0; c < channels; ++c) {
                double v;
                if (r.has_alpha() && c == channels - 1u) {
                    v = 0.3 + 0.7 * double(x) / double(w);
                } else {
                    v = 0.5 + 0.35 * std::sin(x / (17.0 + 5 * c)) * std::cos(y / (23.0 + 3 * c)) + 0.012 * rng.noise();
                }
                const double s = std::round(std::clamp(v, 0.0, 1.0) * maxv);
                r.at(std::size_t(y) * w + x, c) = static_cast<std::uint16_t>(s);
            }
    return r;
}

// GIF de un fotograma con paleta global de 256 colores en degradado.
inline icpu::gif::Image natural_gif(std::uint16_t w, std::uint16_t h, std::uint32_t seed = 2) {
    icpu::gif::Image img;
    img.header = {'G', 'I', 'F', '8', '9', 'a'};
    icpu::put_u16le(img.header, w);
    icpu::put_u16le(img.header, h);
    img.header.push_back(0xF7);  // paleta global de 2^(7+1) = 256 colores
    img.header.push_back(0);
    img.header.push_back(0);
    for (unsigned i = 0; i < 256; ++i) {
        img.global_palette.push_back(static_cast<std::uint8_t>(i));
        img.global_palette.push_back(static_cast<std::uint8_t>((i * 3 + 40) & 0xFF));
        img.global_palette.push_back(static_cast<std::uint8_t>(255 - i));
    }
    icpu::gif::Frame f;
    f.width = w;
    f.height = h;
    f.min_code_size = 8;
    Rng rng(seed);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
            f.indices.push_back(static_cast<std::uint8_t>(
                int(127 + 110 * std::sin(x / 19.0) * std::cos(y / 23.0)) + int(rng.next() % 5) - 2));
    img.frames.push_back(std::move(f));
    return img;
}

// Argon2id minimo: las pruebas no evaluan la resistencia a fuerza bruta.
inline icpu::crypto::KdfParams fast_kdf() { return {1024, 1, 1}; }

template <typename F>
std::optional<icpu::ErrorCode> error_of(F&& f) {
    try {
        f();
    } catch (const icpu::Error& e) {
        return e.code();
    }
    return std::nullopt;
}

}  // namespace test
