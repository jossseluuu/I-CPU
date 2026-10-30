#include "icpu/crypto/random.hpp"

#include "icpu/core/error.hpp"

#if defined(_WIN32)
#  include <windows.h>
#  include <bcrypt.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  if defined(__linux__)
#    include <sys/random.h>
#  endif
#endif

namespace icpu::crypto {

void os_random(std::uint8_t* out, std::size_t size) {
#if defined(_WIN32)
    while (size > 0) {
        const ULONG chunk = size > 0x10000000 ? 0x10000000 : static_cast<ULONG>(size);
        if (BCryptGenRandom(nullptr, out, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            fail(ErrorCode::Internal, "BCryptGenRandom ha fallado");
        out += chunk;
        size -= chunk;
    }
#elif defined(__linux__)
    while (size > 0) {
        const ssize_t n = getrandom(out, size, 0);
        if (n <= 0) fail(ErrorCode::Internal, "getrandom ha fallado");
        out += n;
        size -= static_cast<std::size_t>(n);
    }
#else
    const int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) fail(ErrorCode::Internal, "No se puede abrir /dev/urandom");
    while (size > 0) {
        const ssize_t n = ::read(fd, out, size);
        if (n <= 0) {
            ::close(fd);
            fail(ErrorCode::Internal, "Error leyendo /dev/urandom");
        }
        out += n;
        size -= static_cast<std::size_t>(n);
    }
    ::close(fd);
#endif
}

bool RandomBits::next_bit() {
    if (bit_pos_ == sizeof(buffer_) * 8) {
        os_random(buffer_, sizeof(buffer_));
        bit_pos_ = 0;
    }
    const bool bit = (buffer_[bit_pos_ >> 3] >> (bit_pos_ & 7)) & 1u;
    ++bit_pos_;
    return bit;
}

}  // namespace icpu::crypto
