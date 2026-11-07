#include "icpu/stego/payload.hpp"

#include <algorithm>
#include <array>
#include <cctype>

#include "icpu/compress/zlib.hpp"
#include "icpu/core/error.hpp"

namespace icpu::stego {

namespace {

constexpr std::uint8_t kFlagCompressed = 0x01;
constexpr std::uint8_t kFlagFile = 0x02;
constexpr std::uint8_t kKnownFlags = kFlagCompressed | kFlagFile;
constexpr std::size_t kFixedOverhead = 1 + 1 + 4;

// CON, NUL, COM1... abren dispositivos en Windows, con cualquier extension.
bool reserved_device(std::string_view name) {
    static constexpr std::array<std::string_view, 4> kPlain = {"con", "prn", "aux", "nul"};
    std::string stem(name.substr(0, name.find('.')));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (std::find(kPlain.begin(), kPlain.end(), stem) != kPlain.end()) return true;
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9';
}

// Nombre base de una ruta en UTF-8, aceptando '/' y '\\' como separadores. El nombre
// viene de quien creo la imagen y debe ser seguro en cualquier sistema: "C:x" es una
// ruta relativa a otra carpeta en Windows y "x:y" un flujo oculto de NTFS.
std::string base_name(std::string_view path) {
    const std::size_t cut = path.find_last_of("/\\");
    std::string name(cut == std::string_view::npos ? path : path.substr(cut + 1));
    for (char& c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || std::string_view("<>:\"|?*").find(c) != std::string_view::npos) c = '_';
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    if (reserved_device(name)) name.insert(name.begin(), '_');
    return name;
}

Bytes serialize(const Payload& p, std::uint8_t flags, ByteSpan body) {
    Bytes out;
    out.reserve(kFixedOverhead + p.name.size() + body.size());
    out.push_back(flags);
    out.push_back(static_cast<std::uint8_t>(p.name.size()));
    append(out, as_bytes(p.name));
    put_u32le(out, static_cast<std::uint32_t>(p.data.size()));
    append(out, body);
    return out;
}

}  // namespace

Payload Payload::text(std::string_view message) {
    Payload p;
    p.kind = Kind::Text;
    p.data.assign(message.begin(), message.end());
    return p;
}

Payload Payload::file(std::string name, Bytes content) {
    Payload p;
    p.kind = Kind::File;
    // Nunca se guardan rutas: solo el nombre base, para evitar fugas de
    // informacion del emisor y ataques de "path traversal" al extraer.
    p.name = base_name(name);
    if (p.name.size() > 255) p.name.resize(255);
    p.data = std::move(content);
    return p;
}

std::size_t packed_size_uncompressed(const Payload& payload) {
    return kFixedOverhead + payload.name.size() + payload.data.size();
}

PackedPayload pack_payload(const Payload& payload, bool allow_compression) {
    require(payload.name.size() <= 255, ErrorCode::InvalidArgument, "Nombre de fichero demasiado largo");
    require(payload.data.size() <= compress::kMaxDecompressedBytes, ErrorCode::InvalidArgument,
            "Mensaje demasiado grande (maximo 1 GiB)");
    const std::uint8_t kind_flag = payload.kind == Payload::Kind::File ? kFlagFile : 0;

    if (allow_compression && payload.data.size() >= 16) {
        Bytes deflated = compress::zlib_compress(payload.data, 9);
        if (deflated.size() < payload.data.size())
            return {serialize(payload, kind_flag | kFlagCompressed, deflated), true};
    }
    return {serialize(payload, kind_flag, payload.data), false};
}

Payload unpack_payload(ByteSpan packed) {
    require(packed.size() >= kFixedOverhead, ErrorCode::CorruptData, "Carga util truncada");
    const std::uint8_t flags = packed[0];
    require((flags & ~kKnownFlags) == 0, ErrorCode::CorruptData, "Carga util con flags desconocidos");
    const std::size_t name_len = packed[1];
    require(packed.size() >= kFixedOverhead + name_len, ErrorCode::CorruptData, "Carga util truncada");

    Payload p;
    p.kind = (flags & kFlagFile) ? Payload::Kind::File : Payload::Kind::Text;
    p.name.assign(reinterpret_cast<const char*>(packed.data() + 2), name_len);
    const std::uint32_t original_size = get_u32le(packed.data() + 2 + name_len);
    const ByteSpan body = packed.subspan(kFixedOverhead + name_len);

    if (flags & kFlagCompressed) {
        p.data = compress::zlib_decompress(body, original_size);
    } else {
        require(body.size() == original_size, ErrorCode::CorruptData, "Tamano de carga util inconsistente");
        p.data.assign(body.begin(), body.end());
    }
    if (p.kind == Payload::Kind::File) p.name = base_name(p.name);
    return p;
}

}  // namespace icpu::stego
