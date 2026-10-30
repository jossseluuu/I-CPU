#pragma once
// Contenedor de material secreto de tamano fijo que se borra de memoria al
// destruirse (crypto_wipe evita que el compilador elimine el borrado).

#include <array>
#include <cstddef>
#include <cstdint>

namespace icpu::crypto {

void secure_wipe(void* data, std::size_t size) noexcept;

template <std::size_t N>
class Secret {
public:
    Secret() { bytes_.fill(0); }
    ~Secret() { secure_wipe(bytes_.data(), N); }
    Secret(const Secret& other) : bytes_(other.bytes_) {}
    Secret& operator=(const Secret& other) {
        bytes_ = other.bytes_;
        return *this;
    }

    std::uint8_t* data() { return bytes_.data(); }
    const std::uint8_t* data() const { return bytes_.data(); }
    static constexpr std::size_t size() { return N; }

private:
    std::array<std::uint8_t, N> bytes_;
};

using Key256 = Secret<32>;

}  // namespace icpu::crypto
