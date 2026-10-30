#include "icpu/crypto/hash.hpp"

#include <monocypher.h>

namespace icpu::crypto {

void secure_wipe(void* data, std::size_t size) noexcept { crypto_wipe(data, size); }

struct Blake2b256::Impl {
    crypto_blake2b_ctx ctx;
};

Blake2b256::Blake2b256() : impl_(std::make_unique<Impl>()) { crypto_blake2b_init(&impl_->ctx, 32); }

Blake2b256::~Blake2b256() { crypto_wipe(&impl_->ctx, sizeof(impl_->ctx)); }

void Blake2b256::update(ByteSpan data) { crypto_blake2b_update(&impl_->ctx, data.data(), data.size()); }

void Blake2b256::update_u32(std::uint32_t v) {
    const std::uint8_t le[4] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
                                static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 24)};
    crypto_blake2b_update(&impl_->ctx, le, 4);
}

Digest256 Blake2b256::finish() {
    Digest256 out{};
    crypto_blake2b_final(&impl_->ctx, out.data());
    return out;
}

Digest256 blake2b_256(ByteSpan data) {
    Digest256 out{};
    crypto_blake2b(out.data(), out.size(), data.data(), data.size());
    return out;
}

Key256 derive_subkey(ByteSpan master, std::string_view label) {
    Key256 key;
    const ByteSpan msg = as_bytes(label);
    crypto_blake2b_keyed(key.data(), key.size(), master.data(), master.size(), msg.data(), msg.size());
    return key;
}

}  // namespace icpu::crypto
