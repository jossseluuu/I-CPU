#pragma once
// Carga util: lo que el usuario quiere ocultar (un texto o un fichero con su
// nombre) serializado y opcionalmente comprimido ANTES de cifrarse.
//
//   +-------+----------+--------------+----------------+------------------+
//   | flags | name_len | name (UTF-8) | original_size  | datos (deflate?) |
//   | 1 B   | 1 B      | 0..255 B     | 4 B (LE)       | resto            |
//   +-------+----------+--------------+----------------+------------------+
//   flags: bit0 = comprimido, bit1 = es fichero, resto reservado (0).

#include <string>

#include "icpu/core/bytes.hpp"

namespace icpu::stego {

struct Payload {
    enum class Kind { Text, File };

    Kind kind = Kind::Text;
    std::string name;  // solo para ficheros (nombre base, sin rutas)
    Bytes data;

    static Payload text(std::string_view message);
    static Payload file(std::string name, Bytes content);
    std::string as_text() const { return to_string(data); }
};

struct PackedPayload {
    Bytes bytes;
    bool compressed = false;
};

// Comprime solo si reduce el tamano.
PackedPayload pack_payload(const Payload& payload, bool allow_compression = true);
Payload unpack_payload(ByteSpan packed);

// Tamano de la serializacion sin compresion (cota superior de lo que se insertara).
std::size_t packed_size_uncompressed(const Payload& payload);

}  // namespace icpu::stego
