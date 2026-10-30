#include "icpu/stego/engine.hpp"

#include "icpu/core/bits.hpp"
#include "icpu/core/error.hpp"
#include "icpu/crypto/aead.hpp"

namespace icpu::stego {

namespace {

constexpr char kAuthFailure[] =
    "No se ha encontrado ningun mensaje: la contrasena es incorrecta, la imagen no contiene "
    "datos ocultos o ha sido modificada.";

// Recorre el orden de ranuras saltando las que la portadora marca como no utilizables.
class UsableCursor {
public:
    UsableCursor(const Carrier& carrier, SlotOrder& order) : carrier_(carrier), order_(order) {}

    std::uint64_t next() {
        for (;;) {
            const std::uint64_t slot = order_.next();
            ++visited_;
            if (carrier_.usable(slot)) return slot;
        }
    }
    std::uint64_t visited() const { return visited_; }

private:
    const Carrier& carrier_;
    SlotOrder& order_;
    std::uint64_t visited_ = 0;
};

std::uint8_t encode_params(const EngineParams& p) {
    std::uint8_t v = static_cast<std::uint8_t>(p.bits_per_slot - 1) & 0x03;
    if (p.mode == EmbedMode::Match) v |= 0x04;
    if (p.layout == Layout::Sequential) v |= 0x08;
    return v;
}

std::size_t max_sealed_bytes(const Carrier& carrier, unsigned k) {
    const std::uint64_t usable = carrier.usable_count();
    if (usable <= kHeaderSlots) return 0;
    return static_cast<std::size_t>(((usable - kHeaderSlots) * k) / 8);
}

void validate(const Carrier& carrier, const EngineParams& p) {
    if (p.bits_per_slot < 1 || p.bits_per_slot > carrier.max_bits_per_slot())
        fail(ErrorCode::InvalidArgument, "Bits por ranura fuera de rango para el dominio " +
                                             std::string(carrier.domain()) + " (maximo " +
                                             std::to_string(carrier.max_bits_per_slot()) + ")");
    if (p.mode == EmbedMode::Match && (p.bits_per_slot != 1 || !carrier.supports_matching()))
        fail(ErrorCode::InvalidArgument,
             "LSB matching solo esta disponible con 1 bit por ranura en el dominio espacial");
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

std::size_t max_payload_bytes(const Carrier& carrier, unsigned bits_per_slot) {
    const std::size_t sealed = max_sealed_bytes(carrier, bits_per_slot);
    return sealed > crypto::kMacSize ? sealed - crypto::kMacSize : 0;
}

EmbedStats embed(Carrier& carrier, std::string_view password, ByteSpan packed_payload,
                 const EngineParams& params) {
    validate(carrier, params);
    const unsigned k = params.bits_per_slot;
    const std::size_t capacity = max_payload_bytes(carrier, k);
    if (packed_payload.size() > capacity)
        fail(ErrorCode::Capacity, "El mensaje (" + std::to_string(packed_payload.size()) +
                                      " bytes) no cabe en la portadora con " + std::to_string(k) +
                                      " bit(s) por ranura (maximo " + std::to_string(capacity) + " bytes)");

    // 1. Claves: Argon2id(contrasena, sal derivada de la imagen).
    const crypto::SessionKeys keys = crypto::derive_session_keys(password, carrier_salt(carrier), params.kdf);

    // 2. Cabecera (que se autentica como datos asociados) y cifrado.
    const crypto::Nonce nonce = crypto::random_nonce();
    const std::size_t sealed_size = packed_payload.size() + crypto::kMacSize;
    Bytes header;
    header.reserve(kHeaderBytes);
    header.push_back(kFormatVersion);
    header.push_back(encode_params(params));
    append(header, nonce);
    put_u32le(header, static_cast<std::uint32_t>(sealed_size));
    const Bytes sealed = crypto::seal(keys.encryption, nonce, header, packed_payload);

    // 3. Insercion: cabecera a 1 bit/ranura, cuerpo a k bits/ranura, en el orden secreto.
    auto order = make_slot_order(params.layout, carrier.slot_count(), keys.placement);
    UsableCursor cursor(carrier, *order);
    crypto::RandomBits rng;
    EmbedStats stats;
    stats.sealed_bytes = sealed.size();

    BitReader header_bits(header);
    while (header_bits.remaining() > 0) {
        const std::uint64_t slot = cursor.next();
        stats.slots_changed += carrier.write(slot, header_bits.read(1), 1, params.mode, rng);
        ++stats.slots_used;
    }

    BitReader body_bits(sealed);
    while (body_bits.remaining() > 0) {
        const unsigned take = body_bits.remaining() >= k ? k : static_cast<unsigned>(body_bits.remaining());
        std::uint32_t value = body_bits.read(take);
        if (take < k) {
            // Relleno aleatorio del ultimo grupo para no dejar un patron fijo.
            const unsigned pad = k - take;
            value <<= pad;
            for (unsigned i = 0; i < pad; ++i) value |= std::uint32_t(rng.next_bit()) << i;
        }
        const std::uint64_t slot = cursor.next();
        stats.slots_changed += carrier.write(slot, value, k, params.mode, rng);
        ++stats.slots_used;
    }
    stats.slots_visited = cursor.visited();
    stats.bits_embedded = (header.size() + sealed.size()) * 8;
    return stats;
}

Bytes extract(const Carrier& carrier, std::string_view password, Layout layout, const crypto::KdfParams& kdf) {
    const crypto::SessionKeys keys = crypto::derive_session_keys(password, carrier_salt(carrier), kdf);
    if (carrier.usable_count() <= kHeaderSlots) fail(ErrorCode::Authentication, kAuthFailure);

    auto order = make_slot_order(layout, carrier.slot_count(), keys.placement);
    UsableCursor cursor(carrier, *order);

    BitWriter header_bits(kHeaderBytes);
    for (std::size_t i = 0; i < kHeaderSlots; ++i) header_bits.write(carrier.read(cursor.next(), 1), 1);
    const Bytes header = header_bits.take();

    // Validacion de la cabecera: con una contrasena incorrecta estos campos son
    // aleatorios y casi siempre fallan aqui, antes de leer el resto.
    const std::uint8_t version = header[0];
    const std::uint8_t params = header[1];
    const unsigned k = (params & 0x03u) + 1;
    const bool sequential = (params & 0x08u) != 0;
    const std::uint32_t sealed_size = get_u32le(header.data() + 26);
    const bool plausible = version == kFormatVersion && (params & 0xF0u) == 0 &&
                           k <= carrier.max_bits_per_slot() && sequential == (layout == Layout::Sequential) &&
                           sealed_size >= crypto::kMacSize && sealed_size <= max_sealed_bytes(carrier, k);
    if (!plausible) fail(ErrorCode::Authentication, kAuthFailure);

    BitWriter body_bits(sealed_size);
    std::uint64_t remaining_bits = std::uint64_t(sealed_size) * 8;
    while (remaining_bits > 0) {
        const std::uint32_t value = carrier.read(cursor.next(), k);
        const unsigned take = remaining_bits >= k ? k : static_cast<unsigned>(remaining_bits);
        body_bits.write(value >> (k - take), take);
        remaining_bits -= take;
    }
    const Bytes sealed = body_bits.take();

    crypto::Nonce nonce{};
    std::copy(header.begin() + 2, header.begin() + 26, nonce.begin());
    auto plain = crypto::open(keys.encryption, nonce, header, sealed);
    if (!plain) fail(ErrorCode::Authentication, kAuthFailure);
    return std::move(*plain);
}

}  // namespace icpu::stego
