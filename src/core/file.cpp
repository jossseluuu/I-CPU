#include "icpu/core/file.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

#include "icpu/core/error.hpp"

namespace icpu {

namespace fs = std::filesystem;

Bytes read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail(ErrorCode::Io, "No se puede abrir el fichero: " + path_utf8(path));
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) fail(ErrorCode::Io, "No se puede determinar el tamano de: " + path_utf8(path));
    in.seekg(0, std::ios::beg);
    Bytes data(static_cast<std::size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), size))
        fail(ErrorCode::Io, "Error leyendo el fichero: " + path_utf8(path));
    return data;
}

void write_file(const fs::path& path, ByteSpan data) {
    fs::path tmp = path;
    tmp += ".icpu-tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) fail(ErrorCode::Io, "No se puede crear el fichero: " + path_utf8(path));
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) fail(ErrorCode::Io, "Error escribiendo el fichero: " + path_utf8(path));
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        fail(ErrorCode::Io, "No se puede reemplazar el fichero: " + path_utf8(path));
    }
}

fs::path utf8_path(std::string_view utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

std::string path_utf8(const fs::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

std::string lower_extension(const fs::path& path) {
    std::string ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

}  // namespace icpu
