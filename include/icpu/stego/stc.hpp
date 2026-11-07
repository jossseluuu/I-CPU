#pragma once
// Syndrome-Trellis Codes (Filler, Judas y Fridrich, 2011): oculta m bits en n
// elementos eligiendo, con el algoritmo de Viterbi, el conjunto de cambios de
// menor coste total. El receptor no necesita conocer los costes: le basta con
// leer los LSB y multiplicarlos por la misma matriz de paridad.
//
// La memoria crece con los elementos procesados de una vez (2^kHeight bits por
// elemento), asi que la secuencia se divide en segmentos independientes, que
// ademas se resuelven en paralelo.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace icpu::stego::stc {

inline constexpr unsigned kHeight = 8;       // altura de la submatriz: 256 estados
inline constexpr std::size_t kMaxWidth = 128;  // elementos maximos por bit de mensaje

// Elementos que se usan para ocultar `message_bits`, de los `available` que hay.
std::size_t sequence_length(std::size_t available, std::size_t message_bits);

// Devuelve los LSB que debe tener cada elemento, o nada si los costes infinitos
// no dejan ninguna solucion. Exige lsb.size() == costs.size().
std::optional<std::vector<std::uint8_t>> embed(const std::vector<std::uint8_t>& lsb,
                                               const std::vector<float>& costs,
                                               const std::vector<std::uint8_t>& message);

std::vector<std::uint8_t> extract(const std::vector<std::uint8_t>& lsb, std::size_t message_bits);

}  // namespace icpu::stego::stc
