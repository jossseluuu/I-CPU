#pragma once
// Motor esteganografico: cifra la carga util y la reparte por la portadora.
//
// Contenedor que se esconde. Version 2 (la que se escribe):
//
//   CABECERA (30 bytes, siempre a 1 bit por ranura)                CUERPO (k bits por ranura)
//   +---------------------------+------------------------------+   +-----------------------+
//   | nonce (24 B, aleatorio)   | E(version|params|longitud)   |   | cifrado || MAC (16 B) |
//   +---------------------------+------------------------------+   +-----------------------+
//   E = XChaCha20(clave de cabecera, nonce): 1 + 1 + 4 B (longitud LE) cifrados.
//   params: bits 0-1 = k-1 (bits por ranura), bit 2 = LSB matching,
//           bit 3 = orden secuencial, bits 4-7 = 0 (reservados)
//
// - En la imagen solo hay un nonce aleatorio y texto cifrado: sin la contrasena
//   es indistinguible de ruido, incluso con orden secuencial.
// - La cabecera descifrada (nonce|version|params|longitud) se autentica como
//   "datos asociados" del AEAD: si se altera un solo bit, el descifrado falla.
// - Sal = BLAKE2b(material invariante de la portadora) -> no se almacena.
// - Claves = Argon2id(contrasena, sal) -> {cifrado, posicionamiento, cabecera}.
//
// Version 1 (solo lectura, por compatibilidad): la misma cabecera de 30 bytes
// pero EN CLARO, en el orden version|params|nonce|longitud. Ver docs/adr/0006.

#include <cstdint>
#include <string_view>

#include "icpu/core/bytes.hpp"
#include "icpu/crypto/kdf.hpp"
#include "icpu/stego/carrier.hpp"
#include "icpu/stego/slot_order.hpp"

namespace icpu::stego {

inline constexpr std::uint8_t kFormatVersion = 2;
inline constexpr std::size_t kHeaderBytes = 30;
inline constexpr std::size_t kHeaderSlots = kHeaderBytes * 8;

struct EngineParams {
    unsigned bits_per_slot = 1;  // k: 1..max_bits_per_slot() de la portadora
    EmbedMode mode = EmbedMode::Replace;
    Layout layout = Layout::Random;
    crypto::KdfParams kdf{};
    // 2 = cabecera cifrada. 1 solo existe para probar la compatibilidad de lectura.
    std::uint8_t format_version = kFormatVersion;
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

// Lee contenedores v2 y v1 (con una unica derivacion de clave).
// Lanza Error(Authentication) si la contrasena es incorrecta, no hay mensaje
// o la imagen ha sido modificada (los tres casos son indistinguibles a proposito).
Bytes extract(const Carrier& carrier, std::string_view password, Layout layout = Layout::Random,
              const crypto::KdfParams& kdf = {});

}  // namespace icpu::stego
