#include "icpu/crypto/keystream.hpp"

#include <monocypher.h>

#include "icpu/core/error.hpp"

namespace icpu::crypto {

namespace {
constexpr std::uint8_t kZeroNonce[8] = {};
}

KeyStream::KeyStream(const Key256& key) : key_(key) {}

KeyStream::~KeyStream() { crypto_wipe(buffer_.data(), buffer_.size()); }

void KeyStream::refill() {
    // ChaCha20 con nonce fijo a cero: la clave de posicionamiento es de un solo uso
    // por (contrasena, imagen), asi que no hay reutilizacion de (clave, nonce) peligrosa.
    counter_ = crypto_chacha20_djb(buffer_.data(), nullptr, buffer_.size(), key_.data(), kZeroNonce, counter_);
    pos_ = 0;
}

std::uint64_t KeyStream::next_u64() {
    if (pos_ + 8 > buffer_.size()) refill();
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= std::uint64_t(buffer_[pos_ + i]) << (8 * i);
    pos_ += 8;
    return v;
}

std::uint64_t KeyStream::uniform(std::uint64_t bound) {
    require(bound > 0, ErrorCode::Internal, "KeyStream::uniform con cota 0");
    // Se rechazan los valores del ultimo tramo incompleto para que todos los
    // restos sean equiprobables: @@ICPUL@@ = 2^64 mod bound.
    const std::uint64_t threshold = (0 - bound) % bound;
    for (;;) {
        const std::uint64_t r = next_u64();
        if (r >= threshold) return r % bound;
    }
}

}  // namespace icpu::crypto
