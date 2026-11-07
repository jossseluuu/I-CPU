#include "icpu/stego/palette_carrier.hpp"

#include <limits>

namespace icpu::stego {

std::vector<std::uint8_t> PaletteCarrier::sort_palette(const Bytes& palette) {
    const std::size_t n = palette.size() / 3;
    auto dist = [&](std::size_t a, std::size_t b) {
        long d = 0;
        for (unsigned c = 0; c < 3; ++c) {
            const long x = long(palette[a * 3 + c]) - long(palette[b * 3 + c]);
            d += x * x;
        }
        return d;
    };
    auto luma = [&](std::size_t i) {
        return 299L * palette[i * 3] + 587L * palette[i * 3 + 1] + 114L * palette[i * 3 + 2];
    };
    std::vector<bool> used(n, false);
    std::vector<std::uint8_t> order;
    order.reserve(n);
    std::size_t cur = 0;
    for (std::size_t i = 1; i < n; ++i)
        if (luma(i) < luma(cur)) cur = i;
    for (std::size_t step = 0; step < n; ++step) {
        used[cur] = true;
        order.push_back(static_cast<std::uint8_t>(cur));
        long best = std::numeric_limits<long>::max();
        std::size_t next = cur;
        for (std::size_t j = 0; j < n; ++j)
            if (!used[j] && dist(cur, j) < best) {
                best = dist(cur, j);
                next = j;
            }
        cur = next;
    }
    return order;
}

PaletteCarrier::PaletteCarrier(gif::Image& image) : image_(image) {
    for (const auto& f : image_.frames) {
        FrameInfo info;
        info.first_slot = slots_;
        const Bytes& pal = f.palette(image_.global_palette);
        const auto order = sort_palette(pal);
        const std::size_t n = order.size();
        for (std::size_t r = 0; r < n; ++r) {
            info.order[r] = order[r];
            info.rank[order[r]] = static_cast<std::uint8_t>(r);
        }
        for (std::size_t idx = 0; idx < n; ++idx) {
            const std::size_t partner = info.rank[idx] ^ 1u;
            info.pair_ok[idx] = partner < n && static_cast<int>(idx) != f.transparent_index &&
                                static_cast<int>(info.order[partner]) != f.transparent_index;
        }
        for (std::uint8_t idx : f.indices) usable_ += info.pair_ok[idx] ? 1 : 0;
        slots_ += f.indices.size();
        frames_.push_back(info);
    }
}

std::pair<std::size_t, std::size_t> PaletteCarrier::locate(std::uint64_t slot) const {
    std::size_t f = frames_.size() - 1;
    while (slot < frames_[f].first_slot) --f;
    return {f, static_cast<std::size_t>(slot - frames_[f].first_slot)};
}

bool PaletteCarrier::usable(std::uint64_t slot) const {
    const auto [f, i] = locate(slot);
    return frames_[f].pair_ok[image_.frames[f].indices[i]];
}

std::uint32_t PaletteCarrier::read(std::uint64_t slot, unsigned) const {
    const auto [f, i] = locate(slot);
    return frames_[f].rank[image_.frames[f].indices[i]] & 1u;
}

bool PaletteCarrier::write(std::uint64_t slot, std::uint32_t value, unsigned, EmbedMode, crypto::RandomBits&) {
    const auto [f, i] = locate(slot);
    std::uint8_t& idx = image_.frames[f].indices[i];
    const std::uint8_t before = idx;
    const unsigned rank = (frames_[f].rank[idx] & ~1u) | (value & 1u);
    idx = frames_[f].order[rank];
    return idx != before;
}

bool PaletteCarrier::flip(std::uint64_t slot, crypto::RandomBits& rng) {
    return write(slot, read(slot, 1) ^ 1u, 1, EmbedMode::Replace, rng);
}

double PaletteCarrier::flat_share() const {
    std::uint64_t flat = 0, total = 0;
    for (const auto& f : image_.frames) {
        const std::size_t w = f.width, h = f.height;
        for (std::size_t y = 1; y < h; ++y)
            for (std::size_t x = 1; x < w; ++x) {
                const std::uint8_t v = f.indices[y * w + x];
                flat += v == f.indices[y * w + x - 1] && v == f.indices[(y - 1) * w + x];
                ++total;
            }
    }
    return total ? double(flat) / double(total) : 0;
}

std::vector<float> PaletteCarrier::embedding_costs() const {
    std::vector<float> costs;
    costs.reserve(static_cast<std::size_t>(slots_));
    for (std::size_t fi = 0; fi < frames_.size(); ++fi) {
        const auto& info = frames_[fi];
        const auto& f = image_.frames[fi];
        const Bytes& pal = f.palette(image_.global_palette);
        std::array<float, 256> by_index;
        by_index.fill(std::numeric_limits<float>::infinity());
        for (std::size_t idx = 0; idx < pal.size() / 3; ++idx) {
            if (!info.pair_ok[idx]) continue;
            const std::size_t partner = info.order[info.rank[idx] ^ 1u];
            float distance = 0;
            for (unsigned c = 0; c < 3; ++c) {
                const float d = float(pal[idx * 3 + c]) - float(pal[partner * 3 + c]);
                distance += d * d;
            }
            by_index[idx] = 1.0f + distance;
        }
        for (const std::uint8_t idx : f.indices) costs.push_back(by_index[idx]);
    }
    return costs;
}

void PaletteCarrier::salt_material(crypto::Blake2b256& hash) const {
    hash.update(image_.header);
    hash.update(image_.global_palette);
    Bytes buf;
    buf.reserve(1 << 16);
    for (std::size_t fi = 0; fi < image_.frames.size(); ++fi) {
        const auto& f = image_.frames[fi];
        hash.update_u32(f.width);
        hash.update_u32(f.height);
        hash.update(f.local_palette);
        hash.update_u32(static_cast<std::uint32_t>(f.transparent_index + 1));
        for (std::uint8_t idx : f.indices) {
            buf.push_back(static_cast<std::uint8_t>(frames_[fi].rank[idx] >> 1));
            if (buf.size() >= (1 << 16)) {
                hash.update(buf);
                buf.clear();
            }
        }
    }
    hash.update(buf);
}

}  // namespace icpu::stego
