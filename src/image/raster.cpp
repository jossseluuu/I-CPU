#include "icpu/image/raster.hpp"

#include "icpu/core/error.hpp"
#include "icpu/image/limits.hpp"

namespace icpu::image {

bool dimensions_allowed(std::uint64_t width, std::uint64_t height) {
    return width > 0 && height > 0 && width <= kMaxPixels && height <= kMaxPixels && width * height <= kMaxPixels;
}

void check_dimensions(std::uint64_t width, std::uint64_t height) {
    require(dimensions_allowed(width, height), ErrorCode::UnsupportedFormat,
            "La imagen es demasiado grande (maximo 200 megapixeles)");
}

Raster convert_channels(const Raster& src, std::uint8_t channels) {
    require(channels >= 1 && channels <= 4, ErrorCode::Internal, "Numero de canales invalido");
    if (src.channels == channels) return src;
    Raster out(src.width, src.height, channels, src.depth);
    const bool src_gray = src.channels <= 2;
    const bool dst_gray = channels <= 2;
    for (std::size_t p = 0; p < src.pixel_count(); ++p) {
        std::uint16_t rgb[3];
        if (src_gray) {
            rgb[0] = rgb[1] = rgb[2] = src.at(p, 0);
        } else {
            for (unsigned c = 0; c < 3; ++c) rgb[c] = src.at(p, c);
        }
        const std::uint16_t alpha = src.has_alpha() ? src.at(p, src.channels - 1u) : src.max_value();
        if (dst_gray) {
            // Luminancia ITU-R BT.601 (la misma que usa JPEG).
            out.at(p, 0) = static_cast<std::uint16_t>((299u * rgb[0] + 587u * rgb[1] + 114u * rgb[2] + 500u) / 1000u);
        } else {
            for (unsigned c = 0; c < 3; ++c) out.at(p, c) = rgb[c];
        }
        if (out.has_alpha()) out.at(p, channels - 1u) = alpha;
    }
    return out;
}

Raster convert_depth(const Raster& src, std::uint8_t depth) {
    require(depth == 8 || depth == 16, ErrorCode::Internal, "Profundidad invalida");
    if (src.depth == depth) return src;
    Raster out(src.width, src.height, src.channels, depth);
    for (std::size_t i = 0; i < src.samples.size(); ++i) {
        const std::uint32_t v = src.samples[i];
        out.samples[i] = depth == 8 ? static_cast<std::uint16_t>((v * 255u + 32767u) / 65535u)
                                    : static_cast<std::uint16_t>(v * 257u);
    }
    return out;
}

Raster to_rgb8(const Raster& src) {
    Raster r = convert_depth(src, 8);
    return convert_channels(r, 3);
}

}  // namespace icpu::image
