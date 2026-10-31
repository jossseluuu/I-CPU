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
    if (p.format_version != 1 && p.format_version != 2)
        fail(ErrorCode::InvalidArgument, "Version de formato desconocida");
}

// Cabecera logica, independiente de como se almacene (v1 en claro, v2 cifrada).
struct Header {
    std::uint8_t version = 0;
    std::uint8_t params = 0;
    crypto::Nonce nonce{};
    std::uint32_t sealed_size = 0;

    unsigned bits() const { return (params & 0x03u) + 1; }

    // Datos asociados del AEAD. v1: la cabecera tal como se almacena (en claro).
    // v2: la cabecera descifrada, nonce|version|params|longitud.
    Bytes associated_data() const {
        Bytes ad;
        ad.reserve(kHeaderBytes);
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

// Bytes que se escriben en la imagen.
Bytes store_header(const Header& h, const crypto::SessionKeys& keys) {
    if (h.version == 1) return h.associated_data();
    Bytes out(h.nonce.begin(), h.nonce.end());
    Bytes secret{h.version, h.params};
    put_u32le(secret, h.sealed_size);
    crypto::stream_xor(keys.header, h.nonce, secret.data(), secret.size());
    append(out, secret);
    return out;
}

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

// Con una contrasena incorrecta los campos son aleatorios y casi siempre fallan
// aqui, antes de leer el cuerpo.
bool plausible(const Header& h, std::uint8_t expected_version, const Carrier& carrier, Layout layout) {
    const bool sequential = (h.params & 0x08u) != 0;
    return h.version == expected_version && (h.params & 0xF0u) == 0 && h.bits() <= carrier.max_bits_per_slot() &&
           sequential == (layout == Layout::Sequential) && h.sealed_size >= crypto::kMacSize &&
           h.sealed_size <= max_sealed_bytes(carrier, h.bits());
}

// Lee el cuerpo que sigue a la cabecera recorriendo de nuevo el orden secreto.
Bytes read_body(const Carrier& carrier, Layout layout, const crypto::Key256& placement, const Header& h) {
    auto order = make_slot_order(layout, carrier.slot_count(), placement);
    UsableCursor cursor(carrier, *order);
    for (std::size_t i = 0; i < kHeaderSlots; ++i) cursor.next();
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
    Header h;
    h.version = params.format_version;
    h.params = encode_params(params);
    h.nonce = crypto::random_nonce();
    h.sealed_size = static_cast<std::uint32_t>(packed_payload.size() + crypto::kMacSize);
    const Bytes sealed = crypto::seal(keys.encryption, h.nonce, h.associated_data(), packed_payload);
    const Bytes header = store_header(h, keys);

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
    const Bytes raw = header_bits.take();

    // Se prueba primero v2 y despues v1: ambas comparten claves y posiciones,
    // solo cambia como se interpretan los 30 bytes de la cabecera.
    const Header candidates[] = {parse_v2(raw, keys), parse_v1(raw)};
    const std::uint8_t versions[] = {2, 1};
    for (std::size_t i = 0; i < 2; ++i) {
        const Header& h = candidates[i];
        if (!plausible(h, versions[i], carrier, layout)) continue;
        const Bytes sealed = read_body(carrier, layout, keys.placement, h);
        if (auto plain = crypto::open(keys.encryption, h.nonce, h.associated_data(), sealed)) return std::move(*plain);
    }
    fail(ErrorCode::Authentication, kAuthFailure);
}

}  // namespace icpu::stego
