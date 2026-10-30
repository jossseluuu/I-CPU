#include "icpu/analysis/metrics.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

#include "icpu/core/error.hpp"

namespace icpu::analysis {

namespace {

std::vector<double> luma(const image::Raster& rgb) {
    std::vector<double> y(rgb.pixel_count());
    for (std::size_t p = 0; p < y.size(); ++p)
        y[p] = 0.299 * rgb.at(p, 0) + 0.587 * rgb.at(p, 1) + 0.114 * rgb.at(p, 2);
    return y;
}

// SSIM medio sobre ventanas de 8x8 con paso 4 (variante habitual sin ventana gaussiana).
double ssim(const image::Raster& a, const image::Raster& b) {
    const auto ya = luma(a), yb = luma(b);
    const double c1 = std::pow(0.01 * 255, 2), c2 = std::pow(0.03 * 255, 2);
    const std::uint32_t w = a.width, h = a.height, win = 8, step = 4;
    if (w < win || h < win) return 1.0;
    double total = 0;
    std::size_t windows = 0;
    for (std::uint32_t y0 = 0; y0 + win <= h; y0 += step)
        for (std::uint32_t x0 = 0; x0 + win <= w; x0 += step) {
            double ma = 0, mb = 0;
            for (std::uint32_t y = y0; y < y0 + win; ++y)
                for (std::uint32_t x = x0; x < x0 + win; ++x) {
                    ma += ya[std::size_t(y) * w + x];
                    mb += yb[std::size_t(y) * w + x];
                }
            const double n = win * win;
            ma /= n;
            mb /= n;
            double va = 0, vb = 0, cov = 0;
            for (std::uint32_t y = y0; y < y0 + win; ++y)
                for (std::uint32_t x = x0; x < x0 + win; ++x) {
                    const double da = ya[std::size_t(y) * w + x] - ma, db = yb[std::size_t(y) * w + x] - mb;
                    va += da * da;
                    vb += db * db;
                    cov += da * db;
                }
            va /= n - 1;
            vb /= n - 1;
            cov /= n - 1;
            total += ((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2));
            ++windows;
        }
    return total / double(windows);
}

}  // namespace

QualityMetrics compare(const image::Raster& cover, const image::Raster& stego) {
    require(cover.width == stego.width && cover.height == stego.height, ErrorCode::InvalidArgument,
            "Las imagenes a comparar deben tener las mismas dimensiones");
    const image::Raster a = image::to_rgb8(cover), b = image::to_rgb8(stego);
    QualityMetrics m;
    double sum_sq = 0;
    m.samples = a.samples.size();
    for (std::size_t i = 0; i < a.samples.size(); ++i) {
        const int d = int(a.samples[i]) - int(b.samples[i]);
        if (d != 0) ++m.changed_samples;
        sum_sq += double(d) * d;
        m.max_abs_diff = std::max<unsigned>(m.max_abs_diff, static_cast<unsigned>(std::abs(d)));
    }
    m.mse = sum_sq / double(m.samples);
    m.psnr = m.mse == 0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / m.mse);
    m.ssim = ssim(a, b);
    return m;
}

}  // namespace icpu::analysis
