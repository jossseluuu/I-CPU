#pragma once
// Compresion sin perdida (formato zlib / DEFLATE, RFC 1950/1951).
// Se usa para comprimir el mensaje antes de cifrarlo (despues no tendria
// efecto: un texto cifrado es indistinguible de datos aleatorios) y para los IDAT de PNG.

#include <cstddef>

#include "icpu/core/bytes.hpp"

namespace icpu::compress {

Bytes zlib_compress(ByteSpan data, int level = 9);

// `expected_size` sirve como comprobacion de integridad adicional.
Bytes zlib_decompress(ByteSpan data, std::size_t expected_size);

}  // namespace icpu::compress
