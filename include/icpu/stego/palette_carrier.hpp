#pragma once
// Portadora del dominio de PALETA (GIF), basada en EzStego (Machado, 1996).
//
// Cambiar el LSB de un indice de paleta puede saltar a un color totalmente
// distinto (indice 6 = rojo, indice 7 = azul). EzStego primero ORDENA la
// paleta para que colores vecinos sean parecidos y oculta el bit en la
// paridad de la POSICION del color en esa ordenacion: el pixel solo cambia
// entre los dos colores de su par {2m, 2m+1}, que son muy similares.
//
// Ordenacion usada: cadena del vecino mas cercano en RGB empezando por el
// color mas oscuro (deterministica, depende solo de la paleta).
// Ranuras: todos los pixeles de todos los fotogramas, salvo los que usan el
// color transparente o un par incompleto (invariante por pares).
// Coste adaptativo: distancia entre los dos colores del par.

#include <array>
#include <vector>

#include "icpu/gif/gif.hpp"
#include "icpu/stego/carrier.hpp"

namespace icpu::stego {

class PaletteCarrier final : public Carrier {
public:
    explicit PaletteCarrier(gif::Image& image);

    std::string_view domain() const override { return "paleta"; }
    std::uint64_t slot_count() const override { return slots_; }
    bool usable(std::uint64_t slot) const override;
    std::uint64_t usable_count() const override { return usable_; }
    unsigned max_bits_per_slot() const override { return 1; }
    bool supports_matching() const override { return false; }

    std::uint32_t read(std::uint64_t slot, unsigned bits) const override;
    bool write(std::uint64_t slot, std::uint32_t value, unsigned bits, EmbedMode mode,
               crypto::RandomBits& rng) override;
    bool reserved_usable(std::uint64_t slot) const override { return usable(slot); }
    std::uint64_t reserved_count() const override { return usable_; }
    std::vector<float> embedding_costs() const override;
    std::uint64_t changeable_count() const override { return usable_; }
    double safe_rate() const override { return 0.2; }
    bool flip(std::uint64_t slot, crypto::RandomBits& rng) override;
    double flat_share() const override;
    void salt_material(crypto::Blake2b256& hash) const override;

    // Ordenacion EzStego de una paleta RGB (expuesta para pruebas y analisis).
    static std::vector<std::uint8_t> sort_palette(const Bytes& palette);

private:
    struct FrameInfo {
        std::uint64_t first_slot = 0;
        std::array<std::uint8_t, 256> rank{};   // indice -> posicion en la ordenacion
        std::array<std::uint8_t, 256> order{};  // posicion -> indice
        std::array<bool, 256> pair_ok{};        // por indice: su par existe y no es transparente
    };
    std::pair<std::size_t, std::size_t> locate(std::uint64_t slot) const;

    gif::Image& image_;
    std::vector<FrameInfo> frames_;
    std::uint64_t slots_ = 0;
    std::uint64_t usable_ = 0;
};

}  // namespace icpu::stego
