#pragma once
// Cifrado autenticado XChaCha20-Poly1305 (AEAD). El nonce de 192 bits se
// genera aleatoriamente en cada insercion, por lo que la probabilidad de
// reutilizarlo es despreciable aunque se reutilice la clave.

#include <array>
#include <cstdint>
#include <optional>

#include "icpu/core/bytes.hpp"
#include "icpu/crypto/secret.hpp"

namespace icpu::crypto {

inline constexpr std::size_t kNonceSize = 24;
inline constexpr std::size_t kMacSize = 16;
using Nonce = std::array<std::uint8_t, kNonceSize>;

Nonce random_nonce();

// Devuelve cifrado || MAC(16). `ad` son datos asociados: autenticados pero no cifrados.
Bytes seal(const Key256& key, const Nonce& nonce, ByteSpan ad, ByteSpan plaintext);

// Devuelve std::nullopt si la autenticacion falla (clave incorrecta o datos alterados).
std::optional<Bytes> open(const Key256& key, const Nonce& nonce, ByteSpan ad, ByteSpan sealed);

// Cifrado de flujo XChaCha20 SIN autenticacion (cifrar y descifrar es la misma
// operacion: XOR con el flujo). Solo para datos cuya integridad ya protege otro
// mecanismo; la cabecera v2 se autentica despues como datos asociados del AEAD.
// Nunca usar la misma (clave, nonce) para dos datos distintos.
void stream_xor(const Key256& key, const Nonce& nonce, std::uint8_t* data, std::size_t size);

}  // namespace icpu::crypto
