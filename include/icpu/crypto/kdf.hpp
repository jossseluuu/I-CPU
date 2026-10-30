#pragma once
// Derivacion de claves a partir de la contrasena con Argon2id (RFC 9106).
// Los parametros son fijos en la version 1 del formato: el receptor no tiene
// forma de leerlos antes de derivar la clave (la clave decide donde estan los datos).

#include <cstdint>
#include <string_view>

#include "icpu/crypto/hash.hpp"
#include "icpu/crypto/secret.hpp"

namespace icpu::crypto {

struct KdfParams {
    std::uint32_t memory_kib = 64 * 1024;  // 64 MiB
    std::uint32_t passes = 3;
    std::uint32_t lanes = 1;
};

// Claves de sesion derivadas de una unica clave maestra Argon2id.
struct SessionKeys {
    Key256 encryption;  // XChaCha20-Poly1305
    Key256 placement;   // flujo ChaCha20 que decide las posiciones
};

SessionKeys derive_session_keys(std::string_view password, const Digest256& salt,
                                const KdfParams& params = {});

}  // namespace icpu::crypto
