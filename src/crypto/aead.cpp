#include "icpu/crypto/aead.hpp"

#include <monocypher.h>

#include "icpu/crypto/random.hpp"

namespace icpu::crypto {

Nonce random_nonce() {
    Nonce n{};
    os_random(n.data(), n.size());
    return n;
}

Bytes seal(const Key256& key, const Nonce& nonce, ByteSpan ad, ByteSpan plaintext) {
    Bytes out(plaintext.size() + kMacSize);
    crypto_aead_lock(out.data(), out.data() + plaintext.size(), key.data(), nonce.data(), ad.data(), ad.size(),
                     plaintext.data(), plaintext.size());
    return out;
}

std::optional<Bytes> open(const Key256& key, const Nonce& nonce, ByteSpan ad, ByteSpan sealed) {
    if (sealed.size() < kMacSize) return std::nullopt;
    const std::size_t text_size = sealed.size() - kMacSize;
    Bytes plain(text_size);
    const int rc = crypto_aead_unlock(plain.data(), sealed.data() + text_size, key.data(), nonce.data(),
                                      ad.data(), ad.size(), sealed.data(), text_size);
    if (rc != 0) {
        crypto_wipe(plain.data(), plain.size());
        return std::nullopt;
    }
    return plain;
}

}  // namespace icpu::crypto
