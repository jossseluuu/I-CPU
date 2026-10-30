#pragma once
// Motor esteganografico: cifra la carga util y la reparte por la portadora.
//
// Contenedor que se esconde (version 1):
//
//   CABECERA (30 bytes, siempre a 1 bit por ranura)              CUERPO (k bits por ranura)
//   +---------+--------+---------------+-------------------+    +-------------------------+
//   | version | params | nonce (24 B)  | longitud (4 B LE) |    | cifrado || MAC (16 B)   |
//   +---------+--------+---------------+-------------------+    +-------------------------+
//   params: bits 0-1 = k-1 (bits por ranura), bit 2 = LSB matching,
//           bit 3 = orden secuencial, bits 4-7 = 0 (reservados)
//
// - La cabecera se autentica como "datos asociados" del AEAD: si se altera
//   un solo bit, el descifrado falla.
// - No hay ningun numero magico ni dato en claro reconocible: sin la
//   contrasena, los bits leidos son indistinguibles de ruido.
// - Sal = BLAKE2b(material invariante de la portadora) -> no se almacena.
// - Claves = Argon2id(contrasena, sal) -> {cifrado, posicionamiento}.

#include <cstdint>
#include <string_view>

#include "icpu/core/bytes.hpp"
#include "icpu/crypto/kdf.hpp"
#include "icpu/stego/carrier.hpp"
#include "icpu/stego/slot_order.hpp"

namespace icpu::stego {

inline constexpr std::uint8_t kFormatVersion = 1;
inline constexpr std::size_t kHeaderBytes = 30;
inline constexpr std::size_t kHeaderSlots = kHeaderBytes * 8;

struct EngineParams {
    unsigned bits_per_slot = 1;  // k: 1..max_bits_per_slot() de la portadora
    EmbedMode mode = EmbedMode::Replace;
    Layout layout = Layout::Random;
    crypto::KdfParams kdf{};
};

struct EmbedStats {
    std::uint64_t slots_used = 0;      // ranuras escritas (cabecera + cuerpo)
    std::uint64_t slots_visited = 0;   // incluye las no utilizables que se saltaron
    std::uint64_t slots_changed = 0;   // ranuras cuyo valor cambio realmente
    std::uint64_t bits_embedded = 0;
    std::size_t sealed_bytes = 0;      // cifrado + MAC
};

// Sal derivada de la propia portadora (invariante a la insercion).
crypto::Digest256 carrier_salt(const Carrier& carrier);

// Bytes maximos de carga util empaquetada que caben con k bits por ranura.
std::size_t max_payload_bytes(const Carrier& carrier, unsigned bits_per_slot);

EmbedStats embed(Carrier& carrier, std::string_view password, ByteSpan packed_payload,
                 const EngineParams& params);

// Lanza Error(Authentication) si la contrasena es incorrecta, no hay mensaje
// o la imagen ha sido modificada (los tres casos son indistinguibles a proposito).
Bytes extract(const Carrier& carrier, std::string_view password, Layout layout = Layout::Random,
              const crypto::KdfParams& kdf = {});

}  // namespace icpu::stego
