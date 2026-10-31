#include "icpu/icpu.hpp"

#include <filesystem>

#include "icpu/gif/gif.hpp"
#include "icpu/image/codec.hpp"
#include "icpu/jpeg/jpeg.hpp"
#include "icpu/stego/document.hpp"

namespace icpu {

namespace {

constexpr std::size_t kPayloadOverhead = 6;  // flags + name_len + original_size

std::string extension_of(std::string_view name) {
    return std::filesystem::path(std::string(name)).extension().string();
}

image::ImageFormat detect_readable(ByteSpan data, std::string_view name) {
    const image::ImageFormat f = image::detect_format(data, extension_of(name));
    require(f != image::ImageFormat::Unknown, ErrorCode::UnsupportedFormat,
            "Formato de imagen no reconocido o no soportado");
    return f;
}

}  // namespace

// La version sale de project(VERSION ...) en CMakeLists.txt: una unica fuente de verdad.
const char* version() { return ICPU_VERSION_STRING; }

EmbedResult embed(ByteSpan cover, const Payload& payload, const EmbedOptions& opt, std::string_view cover_name) {
    require(!opt.password.empty(), ErrorCode::InvalidArgument, "Hace falta una contrasena");
    const image::ImageFormat source = detect_readable(cover, cover_name);
    const image::ImageFormat output =
        opt.output_format.value_or(image::can_write(source) ? source : image::ImageFormat::Png);

    auto doc = stego::open_cover(cover, source, output, opt.jpeg_quality);
    stego::Carrier& carrier = doc->carrier();
    const stego::PackedPayload packed = stego::pack_payload(payload, opt.compress);

    // Eleccion de k: el menor numero de bits por ranura con el que cabe el mensaje
    // (menos bits = menos distorsion y menos detectable).
    unsigned k = opt.bits_per_slot;
    const unsigned k_max = opt.mode == EmbedMode::Match ? 1 : carrier.max_bits_per_slot();
    if (k == 0) {
        for (unsigned candidate = 1; candidate <= k_max && k == 0; ++candidate)
            if (packed.bytes.size() <= stego::max_payload_bytes(carrier, candidate)) k = candidate;
        if (k == 0)
            fail(ErrorCode::Capacity,
                 "El mensaje no cabe: necesita " + std::to_string(packed.bytes.size()) + " bytes y la imagen admite " +
                     std::to_string(stego::max_payload_bytes(carrier, k_max)) + " como maximo en el dominio " +
                     std::string(carrier.domain()) + ". Usa una imagen mayor, otro formato o comprime el fichero.");
    }

    stego::EngineParams params;
    params.bits_per_slot = k;
    params.mode = opt.mode;
    params.layout = opt.layout;
    params.kdf = opt.kdf;
    const stego::EmbedStats stats = stego::embed(carrier, opt.password, packed.bytes, params);

    EmbedResult result;
    result.image = doc->serialize();
    result.format = doc->format();
    result.domain = doc->domain();
    result.bits_per_slot = k;
    result.message_bytes = payload.data.size();
    result.packed_bytes = packed.bytes.size();
    result.compressed = packed.compressed;
    result.capacity_bytes = stego::max_payload_bytes(carrier, k);
    result.usable_slots = carrier.usable_count();
    result.stats = stats;
    result.notes = doc->notes;

    if (opt.verify) {
        // Prueba de extremo a extremo: lo que se ha escrito en disco debe devolver el mensaje.
        auto check = stego::open_stego(result.image, std::string("x.") + image::default_extension(result.format));
        const Bytes recovered = stego::extract(check->carrier(), opt.password, opt.layout, opt.kdf);
        if (recovered != packed.bytes)
            fail(ErrorCode::Internal, "La verificacion ha fallado: el mensaje no se recupera de la imagen generada");
        result.verified = true;
    }
    return result;
}

Payload extract(ByteSpan stego_image, const ExtractOptions& opt, std::string_view name) {
    require(!opt.password.empty(), ErrorCode::InvalidArgument, "Hace falta una contrasena");
    auto doc = stego::open_stego(stego_image, extension_of(name));
    const Bytes packed = stego::extract(doc->carrier(), opt.password, opt.layout, opt.kdf);
    return stego::unpack_payload(packed);
}

ImageInfo inspect(ByteSpan file, std::string_view name) {
    ImageInfo info;
    info.format = detect_readable(file, name);
    info.file_bytes = file.size();

    auto add_capacity = [&](image::ImageFormat out) {
        auto doc = stego::open_cover(file, info.format, out, 90);
        stego::Carrier& c = doc->carrier();
        for (unsigned k = 1; k <= c.max_bits_per_slot(); ++k) {
            CapacityEntry e;
            e.output = out;
            e.domain = doc->domain();
            e.bits_per_slot = k;
            e.usable_slots = c.usable_count();
            const std::size_t cap = stego::max_payload_bytes(c, k);
            e.message_bytes = cap > kPayloadOverhead ? cap - kPayloadOverhead : 0;
            info.capacities.push_back(e);
        }
    };

    if (info.format == image::ImageFormat::Jpeg) {
        const jpeg::CoefImage coefs = jpeg::read_coefficients(file);
        info.width = coefs.width;
        info.height = coefs.height;
        info.channels = static_cast<unsigned>(coefs.components.size());
        info.depth = 8;
        info.jpeg_quality = jpeg::estimate_quality(coefs);
        info.jpeg_progressive = coefs.progressive;
        if (coefs.components.size() == 3) {
            const auto& y = coefs.components[0];
            info.jpeg_subsampling = (y.h == 2 && y.v == 2)   ? "4:2:0"
                                    : (y.h == 2 && y.v == 1) ? "4:2:2"
                                    : (y.h == 1 && y.v == 1) ? "4:4:4"
                                                             : "otro";
        }
    } else if (info.format == image::ImageFormat::Gif) {
        const gif::Image g = gif::decode(file);
        info.width = g.width();
        info.height = g.height();
        info.channels = 1;
        info.depth = 8;
        info.frames = static_cast<unsigned>(g.frames.size());
    } else {
        const image::Raster r = image::decode_raster(file, info.format);
        info.width = r.width;
        info.height = r.height;
        info.channels = r.channels;
        info.depth = r.depth;
    }

    if (image::can_write(info.format) && info.format != image::ImageFormat::Png) add_capacity(info.format);
    add_capacity(image::ImageFormat::Png);
    if (info.format != image::ImageFormat::Jpeg) add_capacity(image::ImageFormat::Jpeg);
    return info;
}

}  // namespace icpu
