#pragma once
// Lectura de contenedores v1 y v2. Interno de stego/: el resto usa engine.hpp.

#include <optional>
#include <string_view>

#include "icpu/core/bytes.hpp"
#include "icpu/crypto/kdf.hpp"
#include "icpu/stego/carrier.hpp"
#include "icpu/stego/slot_order.hpp"

namespace icpu::stego {

// Sal de v1 y v2, derivada de la propia portadora.
crypto::Digest256 carrier_salt(const Carrier& carrier);

// Nada si la contrasena no abre ningun contenedor antiguo en esta portadora.
std::optional<Bytes> extract_legacy(const Carrier& carrier, std::string_view password, Layout layout,
                                    const crypto::KdfParams& kdf);

}  // namespace icpu::stego
