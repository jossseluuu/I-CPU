#pragma once
// Portadora del dominio FRECUENCIAL (JPEG): cada ranura es un coeficiente
// DCT cuantizado, de todos los componentes.
//
//   - Modo adaptativo: la secuencia son TODOS los coeficientes, y los que no se
//     pueden cambiar (ceros, DC) tienen coste infinito. Un coeficiente no nulo
//     se mueve +-1 en magnitud conservando el signo. Costes: UERD (Guo et al., 2015).
//   - Sustitucion: pares simetricos por magnitud, {2,3} y {-2,-3}, ... Se
//     excluyen 0 y +-1 para que el histograma siga siendo simetrico respecto a
//     0, que es justo lo que no hacia el esquema tipo JSteg de v1 y v2.
//   - La sal y la cabecera van en los LSB de los coeficientes DC: el resto de
//     modos no los toca nunca, asi que esas ranuras son estables. Su coste es el
//     de UERD, y por eso se pueden elegir con STC igual que el cuerpo.
//   - v1 y v2 usaban pares de complemento a dos sin el 1 (legacy_usable_value).

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
    bool reserved_usable(std::uint64_t slot) const override;
    std::uint64_t reserved_count() const override { return reserved_; }
    bool reserved_separate() const override { return true; }
    std::vector<float> reserved_costs() const override { return uerd_costs(true); }
    std::vector<float> embedding_costs() const override { return uerd_costs(false); }
    std::uint64_t changeable_count() const override { return changeable_; }
    double safe_rate() const override { return 0.25; }
    bool flip(std::uint64_t slot, crypto::RandomBits& rng) override;
    void salt_material(crypto::Blake2b256& hash) const override;
    bool legacy_usable(std::uint64_t slot) const override;
    std::uint64_t legacy_usable_count() const override { return legacy_usable_; }

    static bool usable_value(std::int16_t coef, unsigned natural_index);
    static bool legacy_usable_value(std::int16_t coef, unsigned natural_index);

private:
    std::int16_t& coef(std::uint64_t slot);
    const std::int16_t& coef(std::uint64_t slot) const;
    // Coste finito en los DC (reserved) o en los AC cambiables.
    std::vector<float> uerd_costs(bool reserved) const;

    jpeg::CoefImage& image_;
    std::vector<std::uint64_t> offsets_;  // primer slot de cada componente
    std::uint64_t slots_ = 0;
    std::uint64_t usable_ = 0;
    std::uint64_t legacy_usable_ = 0;
    std::uint64_t reserved_ = 0;
    std::uint64_t changeable_ = 0;
};

}  // namespace icpu::stego
