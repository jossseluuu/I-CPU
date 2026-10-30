#pragma once
// Metricas de distorsion entre la imagen original (portadora) y la estego.
//
//   MSE  = (1/N) * sum (x_i - y_i)^2
//   PSNR = 10 * log10(MAX^2 / MSE)      [dB]   (> 40 dB: diferencia imperceptible)
//   SSIM = similitud estructural (Wang et al., 2004), en [-1, 1]; 1 = identicas.

#include <cstdint>

#include "icpu/image/raster.hpp"

namespace icpu::analysis {

struct QualityMetrics {
    double mse = 0;
    double psnr = 0;  // infinito si las imagenes son identicas
    double ssim = 1;
    std::uint64_t samples = 0;
    std::uint64_t changed_samples = 0;
    unsigned max_abs_diff = 0;
    double changed_ratio() const { return samples ? double(changed_samples) / double(samples) : 0.0; }
};

// Ambas imagenes deben tener las mismas dimensiones; se comparan en RGB de 8 bits.
QualityMetrics compare(const image::Raster& cover, const image::Raster& stego);

}  // namespace icpu::analysis
