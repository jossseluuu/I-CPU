#include "icpu/core/bits.hpp"

#include "icpu/core/error.hpp"

namespace icpu {

std::uint32_t BitReader::read(unsigned count) {
    require(count >= 1 && count <= 32, ErrorCode::Internal, "BitReader: numero de bits invalido");
    require(remaining() >= count, ErrorCode::Internal, "BitReader: lectura fuera de rango");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i, ++pos_) {
        const unsigned bit = (data_[pos_ >> 3] >> (7 - (pos_ & 7))) & 1u;
        value = (value << 1) | bit;
    }
    return value;
}

void BitWriter::write(std::uint32_t value, unsigned count) {
    require(count >= 1 && count <= 32, ErrorCode::Internal, "BitWriter: numero de bits invalido");
    for (int i = static_cast<int>(count) - 1; i >= 0; --i, ++bits_) {
        if ((bits_ & 7) == 0) out_.push_back(0);
        if ((value >> i) & 1u) out_.back() |= static_cast<std::uint8_t>(0x80u >> (bits_ & 7));
    }
}

Bytes BitWriter::take() {
    bits_ = 0;
    return std::move(out_);
}

}  // namespace icpu
