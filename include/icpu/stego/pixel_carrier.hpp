#pragma once
// Portadora del dominio ESPACIAL: cada ranura es una muestra de color
// (R, G, B o gris) de un pixel. El canal alfa no se modifica nunca.
//
// - Hasta 4 bits por muestra (k = 1..4).
// - Sal: se calcula con los 4 bits ALTOS de cada muestra, que no cambian al
//   insertar (replacement toca solo los k <= 4 bits bajos; matching se fuerza
//   a no propagar acarreo mas alla del bit 3, ver write()).

#include "icpu/image/raster.hpp"
#include "icpu/stego/carrier.hpp"

namespace icpu::stego {

class PixelCarrier final : public Carrier {
public:
    explicit PixelCarrier(image::Raster& raster);

    std::string_view domain() const override { return "espacial"; }
    std::uint64_t slot_count() const override { return slots_; }
    bool usable(std::uint64_t) const override { return true; }
    std::uint64_t usable_count() const override { return slots_; }
    unsigned max_bits_per_slot() const override { return 4; }
    bool supports_matching() const override { return true; }

    std::uint32_t read(std::uint64_t slot, unsigned bits) const override;
    bool write(std::uint64_t slot, std::uint32_t value, unsigned bits, EmbedMode mode,
               crypto::RandomBits& rng) override;
    void salt_material(crypto::Blake2b256& hash) const override;

private:
    std::size_t sample_index(std::uint64_t slot) const;

    image::Raster& raster_;
    std::uint64_t slots_;
};

}  // namespace icpu::stego
