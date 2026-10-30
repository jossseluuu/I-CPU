#include "icpu/image/pnm.hpp"

#include <cctype>
#include <string>

#include "icpu/core/error.hpp"

namespace icpu::image {

namespace {

class Tokenizer {
public:
    explicit Tokenizer(ByteSpan d) : d_(d) {}

    std::uint32_t number() {
        skip_space_and_comments();
        require(pos_ < d_.size() && std::isdigit(d_[pos_]), ErrorCode::CorruptData, "Cabecera PNM invalida");
        std::uint64_t v = 0;
        while (pos_ < d_.size() && std::isdigit(d_[pos_])) {
            v = v * 10 + (d_[pos_++] - '0');
            require(v <= 0xFFFFFFFFull, ErrorCode::CorruptData, "Numero PNM demasiado grande");
        }
        return static_cast<std::uint32_t>(v);
    }
    // Bit de PBM ASCII: los digitos pueden ir pegados ("0101").
    std::uint32_t bit() {
        skip_space_and_comments();
        require(pos_ < d_.size(), ErrorCode::CorruptData, "PBM truncado");
        return d_[pos_++] == '1' ? 1u : 0u;
    }
    // Tras la cabecera de un formato binario hay exactamente un espacio.
    std::size_t binary_start() { return pos_ + 1; }

private:
    void skip_space_and_comments() {
        while (pos_ < d_.size()) {
            if (d_[pos_] == '#') {
                while (pos_ < d_.size() && d_[pos_] != '\n') ++pos_;
            } else if (std::isspace(d_[pos_])) {
                ++pos_;
            } else {
                break;
            }
        }
    }

    ByteSpan d_;
    std::size_t pos_ = 2;
};

}  // namespace

Raster pnm_decode(ByteSpan d) {
    require(d.size() >= 3 && d[0] == 'P' && d[1] >= '1' && d[1] <= '6', ErrorCode::CorruptData,
            "Cabecera PNM invalida");
    const int kind = d[1] - '0';
    Tokenizer tok(d);
    const std::uint32_t w = tok.number(), h = tok.number();
    const bool bitmap = kind == 1 || kind == 4;
    const std::uint32_t maxval = bitmap ? 1 : tok.number();
    require(w > 0 && h > 0 && maxval > 0 && maxval <= 65535, ErrorCode::CorruptData, "Cabecera PNM invalida");
    require(std::uint64_t(w) * h <= 400'000'000ull, ErrorCode::UnsupportedFormat, "Imagen PNM demasiado grande");

    const std::uint8_t channels = (kind == 3 || kind == 6) ? 3 : 1;
    const std::uint8_t depth = maxval > 255 ? 16 : 8;
    Raster r(w, h, channels, depth);
    const std::uint32_t out_max = r.max_value();
    auto scale = [&](std::uint32_t v) {
        require(v <= maxval, ErrorCode::CorruptData, "Muestra PNM fuera de rango");
        return static_cast<std::uint16_t>((v * out_max + maxval / 2) / maxval);
    };

    const std::size_t n = r.samples.size();
    if (kind == 1) {
        for (std::size_t i = 0; i < n; ++i) r.samples[i] = tok.bit() ? 0 : 255;  // 1 = negro
    } else if (kind == 2 || kind == 3) {
        for (std::size_t i = 0; i < n; ++i) r.samples[i] = scale(tok.number());
    } else if (kind == 4) {
        const std::size_t start = tok.binary_start(), row_bytes = (w + 7) / 8;
        require(d.size() >= start + row_bytes * h, ErrorCode::CorruptData, "PBM truncado");
        for (std::uint32_t y = 0; y < h; ++y)
            for (std::uint32_t x = 0; x < w; ++x) {
                const bool black = (d[start + y * row_bytes + x / 8] >> (7 - x % 8)) & 1;
                r.samples[std::size_t(y) * w + x] = black ? 0 : 255;
            }
    } else {
        const std::size_t start = tok.binary_start(), bps = maxval > 255 ? 2 : 1;
        require(d.size() >= start + n * bps, ErrorCode::CorruptData, "PNM truncado");
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint32_t v = bps == 2 ? get_u16be(d.data() + start + 2 * i) : d[start + i];
            r.samples[i] = (maxval == 255 || maxval == 65535) ? static_cast<std::uint16_t>(v) : scale(v);
        }
    }
    return r;
}

Bytes pnm_encode(const Raster& r) {
    require(r.channels == 1 || r.channels == 3, ErrorCode::Internal, "PNM requiere 1 o 3 canales");
    const std::string header = std::string(r.channels == 1 ? "P5" : "P6") + "\n" + std::to_string(r.width) + " " +
                               std::to_string(r.height) + "\n" + std::to_string(r.max_value()) + "\n";
    Bytes out(header.begin(), header.end());
    out.reserve(header.size() + r.samples.size() * (r.depth / 8u));
    for (std::uint16_t v : r.samples) {
        if (r.depth == 16) put_u16be(out, v);
        else out.push_back(static_cast<std::uint8_t>(v));
    }
    return out;
}

}  // namespace icpu::image
