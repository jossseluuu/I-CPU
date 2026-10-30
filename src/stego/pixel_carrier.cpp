#include "icpu/stego/pixel_carrier.hpp"

namespace icpu::stego {

PixelCarrier::PixelCarrier(image::Raster& raster)
    : raster_(raster), slots_(std::uint64_t(raster.pixel_count()) * raster.color_channels()) {}

std::size_t PixelCarrier::sample_index(std::uint64_t slot) const {
    const unsigned cc = raster_.color_channels();
    return static_cast<std::size_t>((slot / cc) * raster_.channels + slot % cc);
}

std::uint32_t PixelCarrier::read(std::uint64_t slot, unsigned bits) const {
    return raster_.samples[sample_index(slot)] & ((1u << bits) - 1u);
}

bool PixelCarrier::write(std::uint64_t slot, std::uint32_t value, unsigned bits, EmbedMode mode,
                         crypto::RandomBits& rng) {
    std::uint16_t& s = raster_.samples[sample_index(slot)];
    const std::uint16_t before = s;
    if (mode == EmbedMode::Replace) {
        const std::uint16_t mask = static_cast<std::uint16_t>((1u << bits) - 1u);
        s = static_cast<std::uint16_t>((s & ~mask) | (value & mask));
    } else if ((s & 1u) != (value & 1u)) {
        // LSB matching: sumar o restar 1 cambia el LSB igual que sustituirlo, pero
        // no crea los "pares de valores" que delata el ataque chi-cuadrado.
        // En los extremos del nibble bajo se fuerza la direccion para que el
        // acarreo no altere los bits altos (de los que depende la sal).
        const unsigned low = s & 0x0Fu;
        if (low == 0x0F) --s;
        else if (low == 0x00) ++s;
        else if (rng.next_bit()) ++s;
        else --s;
    }
    return s != before;
}

void PixelCarrier::salt_material(crypto::Blake2b256& hash) const {
    hash.update_u32(raster_.width);
    hash.update_u32(raster_.height);
    hash.update_u32(raster_.channels);
    hash.update_u32(raster_.depth);
    // Muestras >> 4 (y el alfa completo, que nunca se modifica), en bloques.
    Bytes buf;
    buf.reserve(1 << 16);
    const unsigned cc = raster_.color_channels();
    for (std::size_t i = 0; i < raster_.samples.size(); ++i) {
        const bool is_alpha = (i % raster_.channels) >= cc;
        const std::uint16_t v = is_alpha ? raster_.samples[i] : static_cast<std::uint16_t>(raster_.samples[i] >> 4);
        buf.push_back(static_cast<std::uint8_t>(v));
        if (raster_.depth == 16) buf.push_back(static_cast<std::uint8_t>(v >> 8));
        if (buf.size() >= (1 << 16) - 2) {
            hash.update(buf);
            buf.clear();
        }
    }
    hash.update(buf);
}

}  // namespace icpu::stego
