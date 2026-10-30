#pragma once
// Conversion entre bytes y flujos de bits. El orden es MSB primero dentro de
// cada byte, que es el convenio que usa el formato del contenedor.

#include <cstdint>

#include "icpu/core/bytes.hpp"

namespace icpu {

class BitReader {
public:
    explicit BitReader(ByteSpan data) : data_(data) {}

    // Lee `count` bits (1..32) y los devuelve alineados a la derecha.
    std::uint32_t read(unsigned count);
    std::size_t remaining() const { return data_.size() * 8 - pos_; }

private:
    ByteSpan data_;
    std::size_t pos_ = 0;
};

class BitWriter {
public:
    explicit BitWriter(std::size_t expected_bytes = 0) { out_.reserve(expected_bytes); }

    void write(std::uint32_t value, unsigned count);
    std::size_t bit_count() const { return bits_; }
    // Devuelve los bytes completos; los bits sobrantes del ultimo byte se rellenan con 0.
    Bytes take();

private:
    Bytes out_;
    std::size_t bits_ = 0;
};

}  // namespace icpu
