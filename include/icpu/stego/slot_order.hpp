#pragma once
// Orden en que se recorren las ranuras de la portadora.
//
// RandomOrder implementa un Fisher-Yates "perezoso" alimentado por ChaCha20:
// genera la permutacion elemento a elemento, solo hasta donde se necesita.
// Empieza guardando los intercambios en una tabla hash (memoria O(m) para m
// ranuras usadas) y pasa a un vector denso si se usa mas de 1/8 de la imagen.
// Ambas representaciones producen exactamente la misma secuencia.

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "icpu/crypto/keystream.hpp"

namespace icpu::stego {

enum class Layout {
    Random,     // permutacion secreta dependiente de la clave (por defecto)
    Sequential  // 0, 1, 2, ... (solo para experimentos: es trivialmente detectable)
};

class SlotOrder {
public:
    virtual ~SlotOrder() = default;
    // Siguiente ranura; lanza Error(Capacity) si se han agotado.
    virtual std::uint64_t next() = 0;
};

class SequentialOrder final : public SlotOrder {
public:
    explicit SequentialOrder(std::uint64_t n) : n_(n) {}
    std::uint64_t next() override;

private:
    std::uint64_t n_;
    std::uint64_t i_ = 0;
};

class RandomOrder final : public SlotOrder {
public:
    RandomOrder(std::uint64_t n, const crypto::Key256& key);
    std::uint64_t next() override;

private:
    std::uint64_t get(std::uint64_t k) const;
    void set(std::uint64_t k, std::uint64_t v);
    void densify();

    std::uint64_t n_;
    std::uint64_t i_ = 0;
    crypto::KeyStream stream_;
    std::unordered_map<std::uint64_t, std::uint64_t> sparse_;
    std::vector<std::uint32_t> dense_;
    bool is_dense_ = false;
};

std::unique_ptr<SlotOrder> make_slot_order(Layout layout, std::uint64_t n, const crypto::Key256& key);

}  // namespace icpu::stego
