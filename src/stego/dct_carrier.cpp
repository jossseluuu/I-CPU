#include "icpu/stego/dct_carrier.hpp"

namespace icpu::stego {

DctCarrier::DctCarrier(jpeg::CoefImage& image) : image_(image) {
    for (const auto& comp : image_.components) {
        offsets_.push_back(slots_);
        slots_ += comp.coefs.size();
        for (std::size_t i = 0; i < comp.coefs.size(); ++i)
            usable_ += usable_value(comp.coefs[i], static_cast<unsigned>(i % 64)) ? 1 : 0;
    }
}

bool DctCarrier::usable_value(std::int16_t c, unsigned natural_index) {
    if (natural_index == 0) return false;     // DC
    const int pair = c >> 1;                  // desplazamiento aritmetico: -1 >> 1 == -1
    return pair != 0 && pair != -512;         // {0,1} y {-1024,-1023}
}

std::int16_t& DctCarrier::coef(std::uint64_t slot) {
    std::size_t c = offsets_.size() - 1;
    while (slot < offsets_[c]) --c;
    return image_.components[c].coefs[static_cast<std::size_t>(slot - offsets_[c])];
}

const std::int16_t& DctCarrier::coef(std::uint64_t slot) const {
    return const_cast<DctCarrier*>(this)->coef(slot);
}

bool DctCarrier::usable(std::uint64_t slot) const {
    return usable_value(coef(slot), static_cast<unsigned>(slot % 64));
}

std::uint32_t DctCarrier::read(std::uint64_t slot, unsigned) const {
    return static_cast<std::uint32_t>(coef(slot) & 1);
}

bool DctCarrier::write(std::uint64_t slot, std::uint32_t value, unsigned, EmbedMode, crypto::RandomBits&) {
    std::int16_t& c = coef(slot);
    const std::int16_t before = c;
    // En complemento a dos, sustituir el LSB mantiene el par {2m, 2m+1}: 2<->3, -1<->-2.
    c = static_cast<std::int16_t>((c & ~1) | static_cast<int>(value & 1u));
    return c != before;
}

void DctCarrier::salt_material(crypto::Blake2b256& hash) const {
    hash.update_u32(image_.width);
    hash.update_u32(image_.height);
    Bytes buf;
    buf.reserve(1 << 16);
    for (const auto& comp : image_.components) {
        hash.update_u32(static_cast<std::uint32_t>(comp.id));
        hash.update_u32(static_cast<std::uint32_t>(comp.h * 16 + comp.v));
        for (std::uint16_t q : comp.qtable) hash.update_u32(q);
        for (std::size_t i = 0; i < comp.coefs.size(); ++i) {
            const int v = (i % 64 == 0) ? comp.coefs[i] : (comp.coefs[i] >> 1);
            put_u16le(buf, static_cast<std::uint16_t>(v));
            if (buf.size() >= (1 << 16) - 2) {
                hash.update(buf);
                buf.clear();
            }
        }
    }
    hash.update(buf);
}

}  // namespace icpu::stego
