// Lectura de los contenedores v1 y v2 (I-CPU 1.0 y 1.5). Ya no se escriben; este
// fichero solo existe para que ningun mensaje antiguo se pierda. Las pruebas usan
// imagenes reales de esas versiones (tests/golden).

#include "legacy.hpp"

#include <algorithm>

#include "icpu/core/bits.hpp"
#include "icpu/crypto/aead.hpp"
#include "icpu/stego/engine.hpp"
#include "icpu/stego/slot_order.hpp"

namespace icpu::stego {

namespace {

constexpr std::size_t kLegacyHeaderBytes = 30;
constexpr std::size_t kLegacyHeaderSlots = kLegacyHeaderBytes * 8;

// Recorre el orden de ranuras saltando las que la portadora marca como no utilizables.
class UsableCursor {
public:
    UsableCursor(const Carrier& carrier, SlotOrder& order) : carrier_(carrier), order_(order) {}

    std::uint64_t next() {
        for (;;) {
            const std::uint64_t slot = order_.next();
            if (carrier_.legacy_usable(slot)) return slot;
        }
    }

private:
    const Carrier& carrier_;
    SlotOrder& order_;
};

struct Header {
    std::uint8_t version = 0;
    std::uint8_t params = 0;
    crypto::Nonce nonce{};
    std::uint32_t sealed_size = 0;

    unsigned bits() const { return (params & 0x03u) + 1; }

    // v1: la cabecera tal como se almacena. v2: la cabecera descifrada.
    Bytes associated_data() const {
        Bytes ad;
        ad.reserve(kLegacyHeaderBytes);
        if (version == 1) {
            ad.push_back(version);
            ad.push_back(params);
            append(ad, nonce);
        } else {
            append(ad, nonce);
            ad.push_back(version);
            ad.push_back(params);
        }
        put_u32le(ad, sealed_size);
        return ad;
    }
};

Header parse_v1(const Bytes& raw) {
    Header h;
    h.version = raw[0];
    h.params = raw[1];
    std::copy(raw.begin() + 2, raw.begin() + 26, h.nonce.begin());
    h.sealed_size = get_u32le(raw.data() + 26);
    return h;
}

Header parse_v2(const Bytes& raw, const crypto::SessionKeys& keys) {
    Header h;
    std::copy(raw.begin(), raw.begin() + 24, h.nonce.begin());
    std::uint8_t secret[6];
    std::copy(raw.begin() + 24, raw.begin() + 30, secret);
    crypto::stream_xor(keys.header, h.nonce, secret, sizeof secret);
    h.version = secret[0];
    h.params = secret[1];
    h.sealed_size = get_u32le(secret + 2);
    return h;
}

std::size_t max_sealed_bytes(const Carrier& carrier, unsigned k) {
    const std::uint64_t usable = carrier.legacy_usable_count();
    if (usable <= kLegacyHeaderSlots) return 0;
    return static_cast<std::size_t>(((usable - kLegacyHeaderSlots) * k) / 8);
}

bool plausible(const Header& h, std::uint8_t expected_version, const Carrier& carrier, Layout layout) {
    const bool sequential = (h.params & 0x08u) != 0;
    return h.version == expected_version && (h.params & 0xF0u) == 0 && h.bits() <= carrier.max_bits_per_slot() &&
           sequential == (layout == Layout::Sequential) && h.sealed_size >= crypto::kMacSize &&
           h.sealed_size <= max_sealed_bytes(carrier, h.bits());
}

Bytes read_body(const Carrier& carrier, Layout layout, const crypto::Key256& placement, const Header& h) {
    auto order = make_slot_order(layout, carrier.slot_count(), placement);
    UsableCursor cursor(carrier, *order);
    for (std::size_t i = 0; i < kLegacyHeaderSlots; ++i) cursor.next();
    const unsigned k = h.bits();
    BitWriter body_bits(h.sealed_size);
    std::uint64_t remaining_bits = std::uint64_t(h.sealed_size) * 8;
    while (remaining_bits > 0) {
        const std::uint32_t value = carrier.read(cursor.next(), k);
        const unsigned take = remaining_bits >= k ? k : static_cast<unsigned>(remaining_bits);
        body_bits.write(value >> (k - take), take);
        remaining_bits -= take;
    }
    return body_bits.take();
}

}  // namespace

crypto::Digest256 carrier_salt(const Carrier& carrier) {
    crypto::Blake2b256 h;
    h.update("icpu/v1/salt/");
    h.update(carrier.domain());
    h.update_u32(static_cast<std::uint32_t>(carrier.slot_count()));
    h.update_u32(static_cast<std::uint32_t>(carrier.slot_count() >> 32));
    carrier.salt_material(h);
    return h.finish();
}

std::optional<Bytes> extract_legacy(const Carrier& carrier, std::string_view password, Layout layout,
                                    const crypto::KdfParams& kdf) {
    if (carrier.legacy_usable_count() <= kLegacyHeaderSlots) return std::nullopt;
    const crypto::SessionKeys keys = crypto::derive_session_keys(password, carrier_salt(carrier), kdf);

    auto order = make_slot_order(layout, carrier.slot_count(), keys.placement);
    UsableCursor cursor(carrier, *order);
    BitWriter header_bits(kLegacyHeaderBytes);
    for (std::size_t i = 0; i < kLegacyHeaderSlots; ++i) header_bits.write(carrier.read(cursor.next(), 1), 1);
    const Bytes raw = header_bits.take();

    // Ambas versiones comparten claves y posiciones: solo cambia como se
    // interpretan los 30 bytes de la cabecera.
    const Header candidates[] = {parse_v2(raw, keys), parse_v1(raw)};
    const std::uint8_t versions[] = {2, 1};
    for (std::size_t i = 0; i < 2; ++i) {
        const Header& h = candidates[i];
        if (!plausible(h, versions[i], carrier, layout)) continue;
        const Bytes sealed = read_body(carrier, layout, keys.placement, h);
        if (auto plain = crypto::open(keys.encryption, h.nonce, h.associated_data(), sealed)) return plain;
    }
    return std::nullopt;
}

}  // namespace icpu::stego
