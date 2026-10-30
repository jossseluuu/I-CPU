#pragma once
// Portadora del dominio FRECUENCIAL (JPEG): cada ranura es un coeficiente
// DCT cuantizado. Esquema tipo JSteg sobre todos los componentes:
//
//   - Solo coeficientes AC (el DC, la media del bloque, es muy visible).
//   - Se excluyen los valores 0 y 1: hay muchisimos ceros y convertirlos en
//     no-ceros alteraria mucho la estadistica y el tamano del fichero.
//   - Se excluye el par {-1024, -1023} (el valor -1024 no es codificable en AC).
//   - Las exclusiones se hacen por PARES {2m, 2m+1}: escribir el LSB nunca saca
//     un coeficiente de su par, asi que el conjunto de ranuras utilizables es
//     identico antes y despues de insertar (invariante 1 de Carrier).
//   - Sal: coeficiente >> 1 (el par al que pertenece), que tampoco cambia.

#include <vector>

#include "icpu/jpeg/jpeg.hpp"
#include "icpu/stego/carrier.hpp"

namespace icpu::stego {

class DctCarrier final : public Carrier {
public:
    explicit DctCarrier(jpeg::CoefImage& image);

    std::string_view domain() const override { return "dct"; }
    std::uint64_t slot_count() const override { return slots_; }
    bool usable(std::uint64_t slot) const override;
    std::uint64_t usable_count() const override { return usable_; }
    unsigned max_bits_per_slot() const override { return 1; }
    bool supports_matching() const override { return false; }

    std::uint32_t read(std::uint64_t slot, unsigned bits) const override;
    bool write(std::uint64_t slot, std::uint32_t value, unsigned bits, EmbedMode mode,
               crypto::RandomBits& rng) override;
    void salt_material(crypto::Blake2b256& hash) const override;

    static bool usable_value(std::int16_t coef, unsigned natural_index);

private:
    std::int16_t& coef(std::uint64_t slot);
    const std::int16_t& coef(std::uint64_t slot) const;

    jpeg::CoefImage& image_;
    std::vector<std::uint64_t> offsets_;  // primer slot de cada componente
    std::uint64_t slots_ = 0;
    std::uint64_t usable_ = 0;
};

}  // namespace icpu::stego
