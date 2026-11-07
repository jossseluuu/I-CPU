#pragma once
// Limites comunes de los decodificadores. Una imagen manipulada puede declarar
// dimensiones enormes en muy pocos bytes: se comprueban antes de reservar memoria.

#include <cstdint>

namespace icpu::image {

inline constexpr std::uint64_t kMaxPixels = 200'000'000;       // por imagen o fotograma
inline constexpr std::uint64_t kMaxTotalPixels = 500'000'000;  // suma de fotogramas de un GIF

bool dimensions_allowed(std::uint64_t width, std::uint64_t height);

// Lanza Error(UnsupportedFormat) si la imagen supera kMaxPixels.
void check_dimensions(std::uint64_t width, std::uint64_t height);

}  // namespace icpu::image
