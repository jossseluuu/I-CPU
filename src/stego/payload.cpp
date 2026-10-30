#include "icpu/stego/payload.hpp"

#include <limits>

#include "icpu/compress/zlib.hpp"
#include "icpu/core/error.hpp"

namespace icpu::stego {

namespace {

constexpr std::uint8_t kFlagCompressed = 0x01;
constexpr std::uint8_t kFlagFile = 0x02;
constexpr std::uint8_t kKnownFlags = kFlagCompressed | kFlagFile;
constexpr std::size_t kFixedOverhead = 1 + 1 + 4;

// Nombre base de una ruta en UTF-8, aceptando '/' y '\\' como separadores.
std::string base_name(std::string_view path) {
    const std::size_t cut = path.find_last_of("/\\");
    std::string name(cut == std::string_view::npos ? path : path.substr(cut + 1));
    if (name == "." || name == "..") name.clear();
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
    require(payload.data.size() <= std::numeric_limits<std::uint32_t>::max(), ErrorCode::InvalidArgument,
            "Mensaje demasiado grande");
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
