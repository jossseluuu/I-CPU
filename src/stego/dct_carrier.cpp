#include "icpu/stego/dct_carrier.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace icpu::stego {

namespace {

constexpr int kMaxAcMagnitude = 1023;

bool changeable_value(std::int16_t c, unsigned natural_index) {
    return natural_index != 0 && c != 0 && std::abs(int(c)) < kMaxAcMagnitude;
}

}  // namespace

DctCarrier::DctCarrier(jpeg::CoefImage& image) : image_(image) {
    for (const auto& comp : image_.components) {
        offsets_.push_back(slots_);
        slots_ += comp.coefs.size();
        reserved_ += comp.coefs.size() / 64;
        for (std::size_t i = 0; i < comp.coefs.size(); ++i) {
            const auto index = static_cast<unsigned>(i % 64);
            usable_ += usable_value(comp.coefs[i], index) ? 1 : 0;
            legacy_usable_ += legacy_usable_value(comp.coefs[i], index) ? 1 : 0;
            changeable_ += changeable_value(comp.coefs[i], index) ? 1 : 0;
        }
    }
}

bool DctCarrier::usable_value(std::int16_t c, unsigned natural_index) {
    return natural_index != 0 && std::abs(int(c)) >= 2 && std::abs(int(c)) <= kMaxAcMagnitude;
}

bool DctCarrier::legacy_usable_value(std::int16_t c, unsigned natural_index) {
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

bool DctCarrier::legacy_usable(std::uint64_t slot) const {
    return legacy_usable_value(coef(slot), static_cast<unsigned>(slot % 64));
}

bool DctCarrier::reserved_usable(std::uint64_t slot) const { return slot % 64 == 0; }

std::uint32_t DctCarrier::read(std::uint64_t slot, unsigned) const {
    return static_cast<std::uint32_t>(coef(slot) & 1);
}

bool DctCarrier::write(std::uint64_t slot, std::uint32_t value, unsigned, EmbedMode, crypto::RandomBits&) {
    std::int16_t& c = coef(slot);
    const std::int16_t before = c;
    if (slot % 64 == 0) {
        c = static_cast<std::int16_t>((c & ~1) | static_cast<int>(value & 1u));
    } else {
        // Se sustituye el LSB de la magnitud: 2<->3 y -2<->-3, simetrico respecto a 0.
        const int magnitude = (std::abs(int(c)) & ~1) | static_cast<int>(value & 1u);
        c = static_cast<std::int16_t>(c < 0 ? -magnitude : magnitude);
    }
    return c != before;
}

bool DctCarrier::flip(std::uint64_t slot, crypto::RandomBits& rng) {
    std::int16_t& c = coef(slot);
    const int magnitude = std::abs(int(c)) + (rng.next_bit() ? 1 : -1);
    c = static_cast<std::int16_t>(c < 0 ? -magnitude : magnitude);
    return true;
}

std::vector<float> DctCarrier::uerd_costs(bool reserved) const {
    std::vector<float> costs;
    costs.reserve(static_cast<std::size_t>(slots_));
    for (const auto& comp : image_.components) {
        const std::size_t bw = comp.blocks_w, bh = comp.blocks_h;
        // Energia de cada bloque: sus coeficientes AC, ponderados por la cuantizacion.
        std::vector<float> energy(bw * bh, 0.0f);
        for (std::size_t b = 0; b < energy.size(); ++b)
            for (std::size_t i = 1; i < 64; ++i)
                energy[b] += float(std::abs(int(comp.coefs[b * 64 + i]))) * float(comp.qtable[i]);

        for (std::size_t by = 0; by < bh; ++by)
            for (std::size_t bx = 0; bx < bw; ++bx) {
                // UERD: el bloque cuenta entero y sus 8 vecinos con peso 1/4.
                float neighbours = 0;
                for (std::size_t ny = by ? by - 1 : 0; ny <= by + 1 && ny < bh; ++ny)
                    for (std::size_t nx = bx ? bx - 1 : 0; nx <= bx + 1 && nx < bw; ++nx)
                        if (nx != bx || ny != by) neighbours += energy[ny * bw + nx];
                const std::size_t block = by * bw + bx;
                const float disturbance = energy[block] + 0.25f * neighbours + 1e-3f;
                for (unsigned i = 0; i < 64; ++i) {
                    const float q = float(std::max<std::uint16_t>(comp.qtable[i], 1));
                    const bool allowed = reserved ? i == 0 : changeable_value(comp.coefs[block * 64 + i], i);
                    costs.push_back(allowed ? q / disturbance : std::numeric_limits<float>::infinity());
                }
            }
    }
    return costs;
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
