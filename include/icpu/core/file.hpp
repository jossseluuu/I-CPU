#pragma once
// Lectura y escritura de ficheros completos. La escritura es atomica: se
// escribe en un temporal y se renombra, para no dejar nunca una imagen a medias.

#include <filesystem>
#include <string>

#include "icpu/core/bytes.hpp"

namespace icpu {

Bytes read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, ByteSpan data);

// Extension en minusculas y sin el punto ("PNG" -> "png").
std::string lower_extension(const std::filesystem::path& path);

// Conversion entre rutas y texto UTF-8 (en Windows std::string seria ANSI).
std::filesystem::path utf8_path(std::string_view utf8);
std::string path_utf8(const std::filesystem::path& path);

}  // namespace icpu
