#include "icpu/image/qoi.hpp"

#include <array>
#include <cstring>

#include "icpu/core/error.hpp"
#include "icpu/image/limits.hpp"

namespace icpu::image {

namespace {

constexpr std::uint8_t kOpIndex = 0x00, kOpDiff = 0x40, kOpLuma = 0x80, kOpRun = 0xC0;
constexpr std::uint8_t kOpRgb = 0xFE, kOpRgba = 0xFF, kMask2 = 0xC0;
constexpr std::uint8_t kEnd[8] = {0, 0, 0, 0, 0, 0, 0, 1};

struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Rgba&) const = default;
};

unsigned hash(const Rgba& p) { return (p.r * 3u + p.g * 5u + p.b * 7u + p.a * 11u) % 64u; }

}  // namespace

Raster qoi_decode(ByteSpan d) {
    require(d.size() >= 14 + 8 && std::memcmp(d.data(), "qoif", 4) == 0, ErrorCode::CorruptData,
            "Cabecera QOI invalida");
    const std::uint32_t w = get_u32be(d.data() + 4), h = get_u32be(d.data() + 8);
    const std::uint8_t channels = d[12];
    require(w > 0 && h > 0 && (channels == 3 || channels == 4), ErrorCode::CorruptData, "Cabecera QOI invalida");
    check_dimensions(w, h);

    Raster r(w, h, channels, 8);
    std::array<Rgba, 64> index{};
    Rgba px;
    std::size_t pos = 14, run = 0;
    const std::size_t end = d.size() - 8;
    for (std::size_t i = 0; i < r.pixel_count(); ++i) {
        if (run > 0) {
            --run;
        } else if (pos < end) {
            const std::uint8_t b1 = d[pos++];
            if (b1 == kOpRgb) {
                require(pos + 3 <= end, ErrorCode::CorruptData, "QOI truncado");
                px.r = d[pos++]; px.g = d[pos++]; px.b = d[pos++];
            } else if (b1 == kOpRgba) {
                require(pos + 4 <= end, ErrorCode::CorruptData, "QOI truncado");
                px.r = d[pos++]; px.g = d[pos++]; px.b = d[pos++]; px.a = d[pos++];
            } else if ((b1 & kMask2) == kOpIndex) {
                px = index[b1];
            } else if ((b1 & kMask2) == kOpDiff) {
                px.r = static_cast<std::uint8_t>(px.r + ((b1 >> 4) & 3) - 2);
                px.g = static_cast<std::uint8_t>(px.g + ((b1 >> 2) & 3) - 2);
                px.b = static_cast<std::uint8_t>(px.b + (b1 & 3) - 2);
            } else if ((b1 & kMask2) == kOpLuma) {
                require(pos < end, ErrorCode::CorruptData, "QOI truncado");
                const std::uint8_t b2 = d[pos++];
                const int vg = (b1 & 0x3F) - 32;
                px.r = static_cast<std::uint8_t>(px.r + vg - 8 + ((b2 >> 4) & 0x0F));
                px.g = static_cast<std::uint8_t>(px.g + vg);
                px.b = static_cast<std::uint8_t>(px.b + vg - 8 + (b2 & 0x0F));
            } else {
                run = b1 & 0x3F;
            }
            index[hash(px)] = px;
        }
        r.samples[i * channels + 0] = px.r;
        r.samples[i * channels + 1] = px.g;
        r.samples[i * channels + 2] = px.b;
        if (channels == 4) r.samples[i * channels + 3] = px.a;
    }
    return r;
}

Bytes qoi_encode(const Raster& r) {
    require(r.depth == 8 && (r.channels == 3 || r.channels == 4), ErrorCode::Internal,
            "QOI requiere 3 o 4 canales de 8 bits");
    Bytes out;
    out.reserve(14 + r.pixel_count() * (r.channels + 1) + 8);
    append(out, as_bytes("qoif"));
    put_u32be(out, r.width);
    put_u32be(out, r.height);
    out.push_back(r.channels);
    out.push_back(0);  // sRGB con alfa lineal

    std::array<Rgba, 64> index{};
    Rgba prev;
    unsigned run = 0;
    const std::size_t n = r.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        Rgba px;
        px.r = static_cast<std::uint8_t>(r.at(i, 0));
        px.g = static_cast<std::uint8_t>(r.at(i, 1));
        px.b = static_cast<std::uint8_t>(r.at(i, 2));
        px.a = r.channels == 4 ? static_cast<std::uint8_t>(r.at(i, 3)) : prev.a;
        if (px == prev) {
            ++run;
            if (run == 62 || i == n - 1) {
                out.push_back(static_cast<std::uint8_t>(kOpRun | (run - 1)));
                run = 0;
            }
            continue;
        }
        if (run > 0) {
            out.push_back(static_cast<std::uint8_t>(kOpRun | (run - 1)));
            run = 0;
        }
        const unsigned h = hash(px);
        if (index[h] == px) {
            out.push_back(static_cast<std::uint8_t>(kOpIndex | h));
        } else {
            index[h] = px;
            if (px.a == prev.a) {
                const int vr = static_cast<std::int8_t>(px.r - prev.r);
                const int vg = static_cast<std::int8_t>(px.g - prev.g);
                const int vb = static_cast<std::int8_t>(px.b - prev.b);
                const int vg_r = vr - vg, vg_b = vb - vg;
                if (vr > -3 && vr < 2 && vg > -3 && vg < 2 && vb > -3 && vb < 2) {
                    out.push_back(static_cast<std::uint8_t>(kOpDiff | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2)));
                } else if (vg_r > -9 && vg_r < 8 && vg > -33 && vg < 32 && vg_b > -9 && vg_b < 8) {
                    out.push_back(static_cast<std::uint8_t>(kOpLuma | (vg + 32)));
                    out.push_back(static_cast<std::uint8_t>((vg_r + 8) << 4 | (vg_b + 8)));
                } else {
                    out.insert(out.end(), {kOpRgb, px.r, px.g, px.b});
                }
            } else {
                out.insert(out.end(), {kOpRgba, px.r, px.g, px.b, px.a});
            }
        }
        prev = px;
    }
    out.insert(out.end(), std::begin(kEnd), std::end(kEnd));
    return out;
}

}  // namespace icpu::image
