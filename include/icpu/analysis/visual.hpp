#pragma once
// Visualizaciones para el estudio y la memoria:
//   - plano de bits: imagen en blanco/negro del bit k de cada muestra. En una
//     foto natural el plano 0 parece ruido con algo de estructura; con LSB
//     secuencial se ve un "bloque" de ruido puro donde esta el mensaje.
//   - mapa de diferencias: |cover - estego| amplificado.
//   - histograma por canal.

#include <array>
#include <cstdint>
#include <vector>

#include "icpu/image/raster.hpp"

namespace icpu::analysis {

image::Raster bit_plane(const image::Raster& raster, unsigned bit, int channel = -1);
image::Raster difference_map(const image::Raster& cover, const image::Raster& stego, unsigned amplify = 64);
std::vector<std::array<std::uint64_t, 256>> histograms(const image::Raster& raster);

}  // namespace icpu::analysis
