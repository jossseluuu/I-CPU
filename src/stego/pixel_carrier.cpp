#include "icpu/stego/pixel_carrier.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace icpu::stego {

namespace {

// Media en una ventana de lado 2r + 1, con los bordes replicados.
std::vector<float> box_average(const std::vector<float>& in, std::size_t w, std::size_t h, std::size_t r) {
    const auto span = static_cast<std::ptrdiff_t>(r);
    const float scale = 1.0f / float(2 * r + 1);
    auto pass = [&](const std::vector<float>& src, std::size_t lines, std::size_t length, std::size_t line_step,
                    std::size_t item_step) {
        std::vector<float> out(src.size());
        const auto last = static_cast<std::ptrdiff_t>(length) - 1;
        auto at = [&](std::size_t line, std::ptrdiff_t i) {
            return src[line * line_step + static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(i, 0, last)) * item_step];
        };
        for (std::size_t line = 0; line < lines; ++line) {
            double sum = 0;
            for (std::ptrdiff_t i = -span; i <= span; ++i) sum += at(line, i);
            for (std::size_t i = 0; i < length; ++i) {
                out[line * line_step + i * item_step] = float(sum) * scale;
                const auto c = static_cast<std::ptrdiff_t>(i);
                sum += double(at(line, c + span + 1)) - double(at(line, c - span));
            }
        }
        return out;
    };
    return pass(pass(in, h, w, w, 1), w, h, 1, w);
}

// Costes HILL de un plano: residuo de un filtro paso alto, promediado, invertido
// y vuelto a promediar para que la zona de bajo coste sea coherente.
std::vector<float> hill_costs(const std::vector<float>& plane, std::size_t w, std::size_t h) {
    static constexpr int kKernel[3][3] = {{-1, 2, -1}, {2, -4, 2}, {-1, 2, -1}};
    const auto last_x = static_cast<std::ptrdiff_t>(w) - 1, last_y = static_cast<std::ptrdiff_t>(h) - 1;
    std::vector<float> residual(plane.size());
    for (std::size_t y = 0; y < h; ++y)
        for (std::size_t x = 0; x < w; ++x) {
            float sum = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto sx = std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(x) + dx, 0, last_x);
                    const auto sy = std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(y) + dy, 0, last_y);
                    sum += float(kKernel[dy + 1][dx + 1]) *
                           plane[static_cast<std::size_t>(sy) * w + static_cast<std::size_t>(sx)];
                }
            residual[y * w + x] = std::abs(sum);
        }
    std::vector<float> smooth = box_average(residual, w, h, 1);
    for (float& v : smooth) v = 1.0f / (v + 1e-3f);
    return box_average(smooth, w, h, 7);
}

}  // namespace

PixelCarrier::PixelCarrier(image::Raster& raster)
    : raster_(raster), slots_(std::uint64_t(raster.pixel_count()) * raster.color_channels()) {}

std::size_t PixelCarrier::sample_index(std::uint64_t slot) const {
    const unsigned cc = raster_.color_channels();
    return static_cast<std::size_t>((slot / cc) * raster_.channels + slot % cc);
}

std::uint32_t PixelCarrier::read(std::uint64_t slot, unsigned bits) const {
    return raster_.samples[sample_index(slot)] & ((1u << bits) - 1u);
}

bool PixelCarrier::write(std::uint64_t slot, std::uint32_t value, unsigned bits, EmbedMode mode,
                         crypto::RandomBits& rng) {
    std::uint16_t& s = raster_.samples[sample_index(slot)];
    const std::uint16_t before = s;
    if (mode == EmbedMode::Replace) {
        const std::uint16_t mask = static_cast<std::uint16_t>((1u << bits) - 1u);
        s = static_cast<std::uint16_t>((s & ~mask) | (value & mask));
    } else if ((s & 1u) != (value & 1u)) {
        // LSB matching: sumar o restar 1 cambia el LSB igual que sustituirlo, pero
        // no crea los "pares de valores" que delata el ataque chi-cuadrado.
        flip(slot, rng);
    }
    return s != before;
}

bool PixelCarrier::flip(std::uint64_t slot, crypto::RandomBits& rng) {
    std::uint16_t& s = raster_.samples[sample_index(slot)];
    if (s == 0) ++s;
    else if (s == raster_.max_value()) --s;
    else if (rng.next_bit()) ++s;
    else --s;
    return true;
}

double PixelCarrier::flat_share() const {
    const std::size_t w = raster_.width, h = raster_.height, ch = raster_.channels;
    if (w < 2 || h < 2) return 0;
    std::uint64_t flat = 0;
    for (std::size_t y = 1; y < h; ++y)
        for (std::size_t x = 1; x < w; ++x) {
            const std::uint16_t* p = &raster_.samples[(y * w + x) * ch];
            flat += std::equal(p, p + ch, p - ch) && std::equal(p, p + ch, p - w * ch);
        }
    return double(flat) / double((w - 1) * (h - 1));
}

std::vector<float> PixelCarrier::embedding_costs() const {
    const std::size_t w = raster_.width, h = raster_.height;
    const unsigned cc = raster_.color_channels();
    const float scale = raster_.depth == 16 ? 1.0f / 256.0f : 1.0f;
    std::vector<float> costs(static_cast<std::size_t>(slots_));
    std::vector<float> plane(w * h);
    for (unsigned c = 0; c < cc; ++c) {
        for (std::size_t i = 0; i < plane.size(); ++i)
            plane[i] = float(raster_.samples[i * raster_.channels + c]) * scale;
        const std::vector<float> channel = hill_costs(plane, w, h);
        for (std::size_t i = 0; i < channel.size(); ++i) costs[i * cc + c] = channel[i];
    }
    return costs;
}

void PixelCarrier::salt_material(crypto::Blake2b256& hash) const {
    hash.update_u32(raster_.width);
    hash.update_u32(raster_.height);
    hash.update_u32(raster_.channels);
    hash.update_u32(raster_.depth);
    // Muestras >> 4 (y el alfa completo, que nunca se modifica), en bloques.
    Bytes buf;
    buf.reserve(1 << 16);
    const unsigned cc = raster_.color_channels();
    for (std::size_t i = 0; i < raster_.samples.size(); ++i) {
        const bool is_alpha = (i % raster_.channels) >= cc;
        const std::uint16_t v = is_alpha ? raster_.samples[i] : static_cast<std::uint16_t>(raster_.samples[i] >> 4);
        buf.push_back(static_cast<std::uint8_t>(v));
        if (raster_.depth == 16) buf.push_back(static_cast<std::uint8_t>(v >> 8));
        if (buf.size() >= (1 << 16) - 2) {
            hash.update(buf);
            buf.clear();
        }
    }
    hash.update(buf);
}

}  // namespace icpu::stego
