#pragma once
// BLAKE2b: hash incremental (para la sal derivada de la imagen) y PRF con
// clave para derivar subclaves independientes a partir de la clave maestra.

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include "icpu/core/bytes.hpp"
#include "icpu/crypto/secret.hpp"

namespace icpu::crypto {

using Digest256 = std::array<std::uint8_t, 32>;

class Blake2b256 {
public:
    Blake2b256();
    ~Blake2b256();
    Blake2b256(const Blake2b256&) = delete;
    Blake2b256& operator=(const Blake2b256&) = delete;

    void update(ByteSpan data);
    void update(std::string_view text) { update(as_bytes(text)); }
    void update_u32(std::uint32_t v);
    Digest256 finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Digest256 blake2b_256(ByteSpan data);

// subclave = BLAKE2b-256(clave = master, mensaje = etiqueta). Etiquetas distintas
// producen claves computacionalmente independientes (separacion de dominios).
Key256 derive_subkey(ByteSpan master, std::string_view label);

}  // namespace icpu::crypto
