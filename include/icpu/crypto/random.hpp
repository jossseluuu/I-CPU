#pragma once
// Generador aleatorio criptografico del sistema operativo
// (BCryptGenRandom en Windows, getrandom()/urandom en POSIX).
// Se usa para los nonces y para la direccion +-1 del LSB matching.

#include <cstddef>
#include <cstdint>

namespace icpu::crypto {

void os_random(std::uint8_t* out, std::size_t size);

// Fuente de bits aleatorios con buffer, para consumir bit a bit sin llamar al SO cada vez.
class RandomBits {
public:
    bool next_bit();

private:
    std::uint8_t buffer_[256] = {};
    std::size_t bit_pos_ = sizeof(buffer_) * 8;
};

}  // namespace icpu::crypto
