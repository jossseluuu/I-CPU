#include "icpu/compress/zlib.hpp"

#include <cstdlib>
#include <limits>

#include <stb_image.h>

#include "icpu/core/error.hpp"

// stb_image_write exporta su compresor zlib pero no lo declara en la cabecera.
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace icpu::compress {

Bytes zlib_compress(ByteSpan data, int level) {
    require(data.size() < static_cast<std::size_t>(std::numeric_limits<int>::max()), ErrorCode::InvalidArgument,
            "Datos demasiado grandes para comprimir");
    int out_len = 0;
    unsigned char* out = stbi_zlib_compress(const_cast<unsigned char*>(data.data()), static_cast<int>(data.size()),
                                            &out_len, level);
    require(out != nullptr, ErrorCode::Internal, "Fallo en la compresion zlib");
    Bytes result(out, out + out_len);
    std::free(out);
    return result;
}

Bytes zlib_decompress(ByteSpan data, std::size_t expected_size) {
    require(data.size() < static_cast<std::size_t>(std::numeric_limits<int>::max()), ErrorCode::CorruptData,
            "Datos comprimidos demasiado grandes");
    require(expected_size <= kMaxDecompressedBytes, ErrorCode::CorruptData, "Datos descomprimidos demasiado grandes");
    // Bufer de tamano fijo: un flujo que se expanda mas de lo declarado falla en vez de seguir reservando.
    Bytes result(expected_size);
    const int out_len = stbi_zlib_decode_buffer(reinterpret_cast<char*>(result.data()), static_cast<int>(result.size()),
                                                reinterpret_cast<const char*>(data.data()),
                                                static_cast<int>(data.size()));
    require(out_len >= 0, ErrorCode::CorruptData, "Datos comprimidos corruptos");
    require(static_cast<std::size_t>(out_len) == expected_size, ErrorCode::CorruptData,
            "Tamano descomprimido inesperado");
    return result;
}

}  // namespace icpu::compress
