#include "icpu/analysis/visual.hpp"

#include <algorithm>
#include <cstdlib>

#include "icpu/core/error.hpp"

namespace icpu::analysis {

image::Raster bit_plane(const image::Raster& raster, unsigned bit, int channel) {
    const image::Raster src = image::convert_depth(raster, 8);
    image::Raster out(src.width, src.height, channel < 0 ? src.color_channels() : 1, 8);
    for (std::size_t p = 0; p < src.pixel_count(); ++p) {
        if (channel < 0) {
            for (unsigned c = 0; c < out.channels; ++c) out.at(p, c) = ((src.at(p, c) >> bit) & 1u) ? 255 : 0;
        } else {
            out.at(p, 0) = ((src.at(p, static_cast<unsigned>(channel)) >> bit) & 1u) ? 255 : 0;
        }
    }
    return out;
}

image::Raster difference_map(const image::Raster& cover, const image::Raster& stego, unsigned amplify) {
    require(cover.width == stego.width && cover.height == stego.height, ErrorCode::InvalidArgument,
            "Las imagenes deben tener las mismas dimensiones");
    const image::Raster a = image::to_rgb8(cover), b = image::to_rgb8(stego);
    image::Raster out(a.width, a.height, 3, 8);
    for (std::size_t i = 0; i < a.samples.size(); ++i) {
        const unsigned d = static_cast<unsigned>(std::abs(int(a.samples[i]) - int(b.samples[i])));
        out.samples[i] = static_cast<std::uint16_t>(std::min(255u, d * amplify));
    }
    return out;
}

std::vector<std::array<std::uint64_t, 256>> histograms(const image::Raster& raster) {
    const image::Raster src = image::convert_depth(raster, 8);
    std::vector<std::array<std::uint64_t, 256>> h(src.color_channels());
    for (auto& a : h) a.fill(0);
    for (std::size_t p = 0; p < src.pixel_count(); ++p)
        for (unsigned c = 0; c < src.color_channels(); ++c) ++h[c][src.at(p, c)];
    return h;
}

}  // namespace icpu::analysis
