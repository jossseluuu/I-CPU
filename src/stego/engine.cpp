#include "icpu/stego/engine.hpp"

#include <algorithm>
#include <unordered_set>

#include "icpu/core/bits.hpp"
#include "icpu/core/error.hpp"
#include "icpu/crypto/aead.hpp"
#include "icpu/stego/stc.hpp"
#include "legacy.hpp"

namespace icpu::stego {

namespace {

constexpr char kAuthFailure[] =
    "No se ha encontrado ningun mensaje: la contrasena es incorrecta, la imagen no contiene "
    "datos ocultos o ha sido modificada.";

constexpr std::uint8_t kParamMatch = 0x04;
constexpr std::uint8_t kParamSequential = 0x08;
constexpr std::uint8_t kParamAdaptive = 0x10;
constexpr int kStcAttempts = 3;

using SlotSet = std::unordered_set<std::uint64_t>;

std::uint8_t encode_params(const EngineParams& p) {
    std::uint8_t v = static_cast<std::uint8_t>(p.bits_per_slot - 1) & 0x03;
    if (p.mode == EmbedMode::Match) v |= kParamMatch;
    if (p.mode == EmbedMode::Adaptive) v |= kParamAdaptive;
    if (p.layout == Layout::Sequential) v |= kParamSequential;
    return v;
}

// W: ranuras por cada bit de la sal y de la cabecera. Sale de reserved_count(), que
// escribir no cambia, asi que el receptor obtiene el mismo valor sin ninguna clave.
std::size_t side_width(const Carrier& carrier) {
    const std::uint64_t reserved = carrier.reserved_count();
    const std::uint64_t budget = carrier.reserved_separate() ? reserved : reserved / kReservedShare;
    return static_cast<std::size_t>(std::clamp<std::uint64_t>(budget / kReservedSlots, 1, kMaxSideWidth));
}

std::uint64_t reserved_total(const Carrier& carrier) { return std::uint64_t(kReservedSlots) * side_width(carrier); }

// Lo que la sal y la cabecera quitan al cuerpo.
std::uint64_t body_loss(const Carrier& carrier) { return carrier.reserved_separate() ? 0 : reserved_total(carrier); }

std::size_t max_sealed_bytes(const Carrier& carrier, unsigned k) {
    const std::uint64_t usable = carrier.usable_count(), loss = body_loss(carrier);
    if (usable <= loss || carrier.reserved_count() < kReservedSlots) return 0;
    return static_cast<std::size_t>(((usable - loss) * k) / 8);
}

std::size_t max_adaptive_sealed_bytes(const Carrier& carrier) {
    const std::uint64_t changeable = carrier.changeable_count(), loss = body_loss(carrier);
    if (changeable <= loss || carrier.reserved_count() < kReservedSlots) return 0;
    return static_cast<std::size_t>(double(changeable - loss) * carrier.safe_rate() / 8);
}

void validate(const Carrier& carrier, const EngineParams& p) {
    if (p.bits_per_slot < 1 || p.bits_per_slot > carrier.max_bits_per_slot())
        fail(ErrorCode::InvalidArgument, "Bits por ranura fuera de rango para el dominio " +
                                             std::string(carrier.domain()) + " (maximo " +
                                             std::to_string(carrier.max_bits_per_slot()) + ")");
    if (p.mode == EmbedMode::Match && (p.bits_per_slot != 1 || !carrier.supports_matching()))
        fail(ErrorCode::InvalidArgument,
             "LSB matching solo esta disponible con 1 bit por ranura en el dominio espacial");
    if (p.mode == EmbedMode::Adaptive && p.bits_per_slot != 1)
        fail(ErrorCode::InvalidArgument,
             "El modo adaptativo usa 1 bit por ranura; para mas bits usa --mode replace");
}

// Posiciones de la sal: publicas, porque hacen falta antes de tener ninguna clave.
// Dependen solo del dominio y del numero de ranuras.
std::vector<std::uint64_t> salt_slots(const Carrier& carrier) {
    crypto::Blake2b256 h;
    h.update("icpu/v3/public-slots/");
    h.update(carrier.domain());
    h.update_u32(static_cast<std::uint32_t>(carrier.slot_count()));
    h.update_u32(static_cast<std::uint32_t>(carrier.slot_count() >> 32));
    const crypto::Digest256 digest = h.finish();
    crypto::Key256 key;
    std::copy(digest.begin(), digest.end(), key.data());

    RandomOrder order(carrier.slot_count(), key);
    const std::size_t count = kSaltSlots * side_width(carrier);
    std::vector<std::uint64_t> slots;
    slots.reserve(count);
    while (slots.size() < count) {
        const std::uint64_t s = order.next();
        if (carrier.reserved_usable(s)) slots.push_back(s);
    }
    return slots;
}

// Primeras ranuras reservadas del orden secreto que no estan ya ocupadas por la sal.
std::vector<std::uint64_t> header_slots(const Carrier& carrier, Layout layout, const crypto::Key256& placement,
                                        const SlotSet& salt) {
    auto order = make_slot_order(layout, carrier.slot_count(), placement);
    const std::size_t count = kHeaderSlots * side_width(carrier);
    std::vector<std::uint64_t> slots;
    slots.reserve(count);
    while (slots.size() < count) {
        const std::uint64_t s = order->next();
        if (carrier.reserved_usable(s) && salt.count(s) == 0) slots.push_back(s);
    }
    return slots;
}

// Sal de Argon2id: la almacenada, ligada al dominio y al tamano de la portadora.
crypto::Digest256 kdf_salt(const Carrier& carrier, const Salt& stored) {
    crypto::Blake2b256 h;
    h.update("icpu/v3/salt/");
    h.update(carrier.domain());
    h.update_u32(static_cast<std::uint32_t>(carrier.slot_count()));
    h.update_u32(static_cast<std::uint32_t>(carrier.slot_count() >> 32));
    h.update(ByteSpan(stored.data(), stored.size()));
    return h.finish();
}

struct Header {
    std::uint8_t params = 0;
    crypto::Nonce nonce{};
    std::uint32_t sealed_size = 0;

    unsigned bits() const { return (params & 0x03u) + 1; }
    bool adaptive() const { return (params & kParamAdaptive) != 0; }
    bool sequential() const { return (params & kParamSequential) != 0; }

    Bytes secret_part() const {
        Bytes s{kFormatVersion, params};
        put_u32le(s, sealed_size);
        return s;
    }
    // Datos asociados del AEAD: la cabecera descifrada.
    Bytes associated_data() const {
        Bytes ad(nonce.begin(), nonce.end());
        append(ad, secret_part());
        return ad;
    }
};

Bytes store_header(const Header& h, const crypto::SessionKeys& keys) {
    Bytes out(h.nonce.begin(), h.nonce.end());
    Bytes secret = h.secret_part();
    crypto::stream_xor(keys.header, h.nonce, secret.data(), secret.size());
    append(out, secret);
    return out;
}

std::optional<Header> parse_header(const Bytes& raw, const crypto::SessionKeys& keys) {
    Header h;
    std::copy(raw.begin(), raw.begin() + 24, h.nonce.begin());
    std::uint8_t secret[6];
    std::copy(raw.begin() + 24, raw.begin() + 30, secret);
    crypto::stream_xor(keys.header, h.nonce, secret, sizeof secret);
    if (secret[0] != kFormatVersion) return std::nullopt;
    h.params = secret[1];
    h.sealed_size = get_u32le(secret + 2);
    return h;
}

// Con una contrasena incorrecta los campos son aleatorios y casi siempre fallan
// aqui, antes de leer el cuerpo.
bool plausible(const Header& h, const Carrier& carrier, Layout layout) {
    if ((h.params & 0xE0u) != 0 || h.sequential() != (layout == Layout::Sequential)) return false;
    if (h.bits() > carrier.max_bits_per_slot() || h.sealed_size < crypto::kMacSize) return false;
    if (h.adaptive())
        return h.bits() == 1 && (h.params & kParamMatch) == 0 &&
               std::uint64_t(h.sealed_size) * 8 <= carrier.slot_count() - reserved_total(carrier);
    return h.sealed_size <= max_sealed_bytes(carrier, h.bits());
}

// Las primeras `count` ranuras del orden secreto fuera de las reservadas. Con
// `only_usable` se saltan ademas las no utilizables (modos sin STC).
std::vector<std::uint64_t> body_slots(const Carrier& carrier, Layout layout, const crypto::Key256& placement,
                                      const SlotSet& reserved, std::size_t count, bool only_usable) {
    auto order = make_slot_order(layout, carrier.slot_count(), placement);
    std::vector<std::uint64_t> slots;
    slots.reserve(count);
    while (slots.size() < count) {
        const std::uint64_t s = order->next();
        if (reserved.count(s) == 0 && (!only_usable || carrier.usable(s))) slots.push_back(s);
    }
    return slots;
}

std::size_t slots_for_bits(std::uint64_t bits, unsigned k) { return static_cast<std::size_t>((bits + k - 1) / k); }

std::size_t stc_length(const Carrier& carrier, std::size_t sealed_size) {
    return stc::sequence_length(static_cast<std::size_t>(carrier.slot_count() - reserved_total(carrier)),
                                sealed_size * 8);
}

std::vector<std::uint8_t> lsb_of(const Carrier& carrier, const std::vector<std::uint64_t>& slots) {
    std::vector<std::uint8_t> lsb(slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i) lsb[i] = static_cast<std::uint8_t>(carrier.read(slots[i], 1));
    return lsb;
}

std::vector<std::uint8_t> to_bits(ByteSpan bytes) {
    std::vector<std::uint8_t> bits;
    bits.reserve(bytes.size() * 8);
    for (const std::uint8_t b : bytes)
        for (int i = 7; i >= 0; --i) bits.push_back((b >> i) & 1u);
    return bits;
}

Bytes from_bits(const std::vector<std::uint8_t>& bits) {
    Bytes out(bits.size() / 8, 0);
    for (std::size_t i = 0; i < bits.size(); ++i)
        out[i / 8] = static_cast<std::uint8_t>(out[i / 8] | (bits[i] << (7 - i % 8)));
    return out;
}

// LSB finales de la sal o la cabecera. Siempre hay solucion: los costes son finitos.
std::vector<std::uint8_t> plan_side(const Carrier& carrier, const std::vector<std::uint64_t>& slots,
                                    const std::vector<float>& costs, ByteSpan data) {
    std::vector<float> slot_costs(slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i) slot_costs[i] = costs[static_cast<std::size_t>(slots[i])];
    auto target = stc::embed(lsb_of(carrier, slots), slot_costs, to_bits(data));
    require(target.has_value(), ErrorCode::Internal, "No se han podido colocar la sal y la cabecera");
    return std::move(*target);
}

Bytes read_side(const Carrier& carrier, const std::vector<std::uint64_t>& slots, std::size_t bytes) {
    return from_bits(stc::extract(lsb_of(carrier, slots), bytes * 8));
}

// Resultado del cifrado y los cambios ya calculados: asi un fallo de STC se
// reintenta con otra sal sin haber tocado la portadora.
struct Plan {
    Salt salt{};
    crypto::SessionKeys keys;
    Header header;
    Bytes sealed;
    SlotSet reserved;
    std::vector<std::uint64_t> salt_slots, header_slots, body;
    std::vector<std::uint8_t> salt_target, header_target;
    std::vector<std::uint8_t> target;  // cuerpo, solo modo adaptativo
};

Plan make_plan(const Carrier& carrier, std::string_view password, ByteSpan payload, const EngineParams& params,
               const std::vector<std::uint64_t>& salt_positions, const std::vector<float>& side_costs) {
    Plan p;
    crypto::os_random(p.salt.data(), p.salt.size());
    p.keys = crypto::derive_session_keys(password, kdf_salt(carrier, p.salt), params.kdf);

    p.header.params = encode_params(params);
    p.header.nonce = crypto::random_nonce();
    p.header.sealed_size = static_cast<std::uint32_t>(payload.size() + crypto::kMacSize);
    p.sealed = crypto::seal(p.keys.encryption, p.header.nonce, p.header.associated_data(), payload);

    p.salt_slots = salt_positions;
    const SlotSet salt(salt_positions.begin(), salt_positions.end());
    p.header_slots = header_slots(carrier, params.layout, p.keys.placement, salt);
    p.reserved = salt;
    p.reserved.insert(p.header_slots.begin(), p.header_slots.end());
    p.salt_target = plan_side(carrier, p.salt_slots, side_costs, ByteSpan(p.salt.data(), p.salt.size()));
    p.header_target = plan_side(carrier, p.header_slots, side_costs, store_header(p.header, p.keys));

    const bool adaptive = params.mode == EmbedMode::Adaptive;
    const std::size_t count = adaptive ? stc_length(carrier, p.sealed.size())
                                       : slots_for_bits(std::uint64_t(p.sealed.size()) * 8, params.bits_per_slot);
    p.body = body_slots(carrier, params.layout, p.keys.placement, p.reserved, count, !adaptive);
    return p;
}

// Calcula los LSB finales del cuerpo. Falso si no hay solucion con esta permutacion.
bool plan_stc(Plan& p, const Carrier& carrier, const std::vector<float>& costs) {
    std::vector<float> body_costs(p.body.size());
    for (std::size_t i = 0; i < p.body.size(); ++i) body_costs[i] = costs[static_cast<std::size_t>(p.body[i])];
    auto target = stc::embed(lsb_of(carrier, p.body), body_costs, to_bits(p.sealed));
    if (!target) return false;
    p.target = std::move(*target);
    return true;
}

std::optional<Bytes> extract_current(const Carrier& carrier, std::string_view password, Layout layout,
                                     const crypto::KdfParams& kdf) {
    if (carrier.reserved_count() < kReservedSlots) return std::nullopt;
    const std::vector<std::uint64_t> salt_positions = salt_slots(carrier);
    Salt salt{};
    const Bytes stored = read_side(carrier, salt_positions, kSaltBytes);
    std::copy(stored.begin(), stored.end(), salt.begin());
    const crypto::SessionKeys keys = crypto::derive_session_keys(password, kdf_salt(carrier, salt), kdf);

    const SlotSet salt_set(salt_positions.begin(), salt_positions.end());
    const auto header_positions = header_slots(carrier, layout, keys.placement, salt_set);
    const auto header = parse_header(read_side(carrier, header_positions, kHeaderBytes), keys);
    if (!header || !plausible(*header, carrier, layout)) return std::nullopt;

    SlotSet reserved = salt_set;
    reserved.insert(header_positions.begin(), header_positions.end());

    Bytes sealed;
    if (header->adaptive()) {
        const auto body = body_slots(carrier, layout, keys.placement, reserved,
                                     stc_length(carrier, header->sealed_size), false);
        sealed = from_bits(stc::extract(lsb_of(carrier, body), std::size_t(header->sealed_size) * 8));
    } else {
        const unsigned k = header->bits();
        const std::uint64_t total = std::uint64_t(header->sealed_size) * 8;
        const auto body = body_slots(carrier, layout, keys.placement, reserved, slots_for_bits(total, k), true);
        BitWriter bits(header->sealed_size);
        std::uint64_t remaining = total;
        for (const std::uint64_t slot : body) {
            const unsigned take = remaining >= k ? k : static_cast<unsigned>(remaining);
            bits.write(carrier.read(slot, k) >> (k - take), take);
            remaining -= take;
        }
        sealed = bits.take();
    }
    return crypto::open(keys.encryption, header->nonce, header->associated_data(), sealed);
}

}  // namespace

std::size_t max_payload_bytes(const Carrier& carrier, unsigned bits_per_slot) {
    const std::size_t sealed = max_sealed_bytes(carrier, bits_per_slot);
    return sealed > crypto::kMacSize ? sealed - crypto::kMacSize : 0;
}

std::size_t adaptive_payload_bytes(const Carrier& carrier) {
    const std::size_t sealed = max_adaptive_sealed_bytes(carrier);
    return sealed > crypto::kMacSize ? sealed - crypto::kMacSize : 0;
}

EmbedStats embed(Carrier& carrier, std::string_view password, ByteSpan packed_payload, const EngineParams& params) {
    validate(carrier, params);
    const bool adaptive = params.mode == EmbedMode::Adaptive;
    const unsigned k = params.bits_per_slot;
    const std::size_t capacity = adaptive ? adaptive_payload_bytes(carrier) : max_payload_bytes(carrier, k);
    if (packed_payload.size() > capacity)
        fail(ErrorCode::Capacity, "El mensaje (" + std::to_string(packed_payload.size()) +
                                      " bytes) no cabe en la portadora (maximo " + std::to_string(capacity) +
                                      " bytes en este modo)");

    const std::vector<std::uint64_t> salt_positions = salt_slots(carrier);
    const std::vector<float> costs = carrier.embedding_costs();
    const std::vector<float> separate_costs =
        carrier.reserved_separate() ? carrier.reserved_costs() : std::vector<float>{};
    const std::vector<float>& side_costs = carrier.reserved_separate() ? separate_costs : costs;

    Plan plan = make_plan(carrier, password, packed_payload, params, salt_positions, side_costs);
    if (adaptive) {
        bool solved = plan_stc(plan, carrier, costs);
        for (int attempt = 1; !solved && attempt < kStcAttempts; ++attempt) {
            plan = make_plan(carrier, password, packed_payload, params, salt_positions, side_costs);
            solved = plan_stc(plan, carrier, costs);
        }
        if (!solved)
            fail(ErrorCode::Capacity,
                 "La imagen tiene demasiadas zonas que no se pueden modificar para este mensaje. "
                 "Usa una imagen mayor o con mas textura.");
    }

    crypto::RandomBits rng;
    EmbedStats stats;
    stats.sealed_bytes = plan.sealed.size();

    auto write_reserved = [&](const std::vector<std::uint64_t>& slots, const std::vector<std::uint8_t>& bits) {
        for (std::size_t i = 0; i < slots.size(); ++i) {
            stats.slots_changed += carrier.write(slots[i], bits[i], 1, EmbedMode::Match, rng);
            ++stats.slots_used;
        }
    };
    write_reserved(plan.salt_slots, plan.salt_target);
    write_reserved(plan.header_slots, plan.header_target);

    if (adaptive) {
        for (std::size_t i = 0; i < plan.body.size(); ++i)
            if (carrier.read(plan.body[i], 1) != plan.target[i]) stats.slots_changed += carrier.flip(plan.body[i], rng);
        stats.slots_used += plan.body.size();
    } else {
        BitReader body_bits(plan.sealed);
        for (const std::uint64_t slot : plan.body) {
            const unsigned take = body_bits.remaining() >= k ? k : static_cast<unsigned>(body_bits.remaining());
            std::uint32_t value = body_bits.read(take);
            if (take < k) {
                // Relleno aleatorio del ultimo grupo para no dejar un patron fijo.
                const unsigned pad = k - take;
                value <<= pad;
                for (unsigned i = 0; i < pad; ++i) value |= std::uint32_t(rng.next_bit()) << i;
            }
            stats.slots_changed += carrier.write(slot, value, k, params.mode, rng);
            ++stats.slots_used;
        }
    }
    stats.bits_embedded = (kReservedSlots + plan.sealed.size() * 8);
    return stats;
}

Bytes extract(const Carrier& carrier, std::string_view password, Layout layout, const crypto::KdfParams& kdf) {
    if (auto plain = extract_current(carrier, password, layout, kdf)) return std::move(*plain);
    if (auto plain = extract_legacy(carrier, password, layout, kdf)) return std::move(*plain);
    fail(ErrorCode::Authentication, kAuthFailure);
}

}  // namespace icpu::stego
