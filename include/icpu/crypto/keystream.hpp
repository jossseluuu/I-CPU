#pragma once
// Generador pseudoaleatorio determinista y criptograficamente seguro basado en
// ChaCha20: con la misma clave produce siempre la misma secuencia, lo que
// permite al emisor y al receptor recorrer las mismas posiciones.

#include <array>
#include <cstdint>

#include "icpu/crypto/secret.hpp"

namespace icpu::crypto {

class KeyStream {
public:
    explicit KeyStream(const Key256& key);
    ~KeyStream();
    KeyStream(const KeyStream&) = delete;
    KeyStream& operator=(const KeyStream&) = delete;

    std::uint64_t next_u64();
    // Entero uniforme en [0, bound) sin sesgo de modulo (muestreo por rechazo).
    std::uint64_t uniform(std::uint64_t bound);

private:
    void refill();

    Key256 key_;
    std::uint64_t counter_ = 0;
    std::array<std::uint8_t, 1024> buffer_{};
    std::size_t pos_ = 1024;
};

}  // namespace icpu::crypto
