#pragma once
// Motor esteganografico: cifra la carga util y la reparte por la portadora.
//
// Contenedor que se esconde. Version 3 (la que se escribe):
//
//   SAL (16 B) en W*128 ranuras publicas -> la unica parte que se lee sin contrasena
//   CABECERA (30 B) en W*240 secretas    -> nonce (24 B) | E(version|params|longitud)
//   CUERPO en el orden secreto          -> cifrado || MAC (16 B)
//
//   E = XChaCha20(clave de cabecera, nonce): 1 + 1 + 4 B (longitud LE) cifrados.
//   params: bits 0-1 = k-1 (bits por ranura), bit 2 = LSB matching, bit 3 = orden
//           secuencial, bit 4 = adaptativo (STC), bits 5-7 = 0 (reservados)
//
// - La sal es aleatoria en cada insercion, asi que ocultar dos veces en la misma
//   imagen con la misma contrasena da claves y posiciones distintas.
// - Sal y cabecera van en ranuras que ningun modo modifica despues (reserved_usable)
//   y siempre con STC: de cada W ranuras cambian pocas, y en zonas con textura. W se
//   deduce de reserved_count(), asi que no se guarda. Con W = 1 se escriben bit a bit.
// - La cabecera descifrada se autentica como "datos asociados" del AEAD: si se
//   altera un solo bit, el descifrado falla.
// - Claves = Argon2id(contrasena, sal) -> {cifrado, posicionamiento, cabecera}.
//   Sus parametros son fijos: guardarlos revelaria un valor reconocible.
//
// v1 y v2 (solo lectura): ver src/stego/legacy.cpp y docs/tecnico/formato.md.

#include <array>
#include <cstdint>
#include <string_view>

#include "icpu/core/bytes.hpp"
#include "icpu/crypto/kdf.hpp"
#include "icpu/stego/carrier.hpp"
#include "icpu/stego/slot_order.hpp"

namespace icpu::stego {

inline constexpr std::uint8_t kFormatVersion = 3;
inline constexpr std::size_t kSaltBytes = 16;
inline constexpr std::size_t kSaltSlots = kSaltBytes * 8;
inline constexpr std::size_t kHeaderBytes = 30;
inline constexpr std::size_t kHeaderSlots = kHeaderBytes * 8;
inline constexpr std::size_t kReservedSlots = kSaltSlots + kHeaderSlots;  // minimo, con W = 1
inline constexpr std::size_t kMaxSideWidth = 64;   // W maximo
inline constexpr std::size_t kReservedShare = 8;   // si se comparten con el cuerpo, 1/8 como mucho

using Salt = std::array<std::uint8_t, kSaltBytes>;

struct EngineParams {
    unsigned bits_per_slot = 1;  // k: 1..max_bits_per_slot() de la portadora
    EmbedMode mode = EmbedMode::Adaptive;
    Layout layout = Layout::Random;
    crypto::KdfParams kdf{};
};

struct EmbedStats {
    std::uint64_t slots_used = 0;      // ranuras escritas o, con STC, consideradas
    std::uint64_t slots_changed = 0;   // ranuras cuyo valor cambio realmente
    std::uint64_t bits_embedded = 0;
    std::size_t sealed_bytes = 0;      // cifrado + MAC
};

// Bytes maximos de carga util empaquetada con k bits por ranura (Replace y Match).
std::size_t max_payload_bytes(const Carrier& carrier, unsigned bits_per_slot);

// Bytes maximos en modo adaptativo, limitados a una carga prudente.
std::size_t adaptive_payload_bytes(const Carrier& carrier);

EmbedStats embed(Carrier& carrier, std::string_view password, ByteSpan packed_payload,
                 const EngineParams& params);

// Lee contenedores v3, v2 y v1. Si el v3 no abre hay que repetir Argon2id con la
// sal antigua, asi que una contrasena incorrecta tarda el doble.
// Lanza Error(Authentication) si la contrasena es incorrecta, no hay mensaje
// o la imagen ha sido modificada (los tres casos son indistinguibles a proposito).
Bytes extract(const Carrier& carrier, std::string_view password, Layout layout = Layout::Random,
              const crypto::KdfParams& kdf = {});

}  // namespace icpu::stego
