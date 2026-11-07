#include "icpu/image/codec.hpp"

#include <cstdlib>
#include <limits>

#include <stb_image.h>
#include <stb_image_write.h>

#include "icpu/core/error.hpp"
#include "icpu/image/limits.hpp"
#include "icpu/image/pnm.hpp"
#include "icpu/image/qoi.hpp"

namespace icpu::image {

namespace {

Raster decode_with_stb(ByteSpan data) {
    require(data.size() < static_cast<std::size_t>(std::numeric_limits<int>::max()), ErrorCode::UnsupportedFormat,
            "Fichero demasiado grande");
    const auto* buf = data.data();
    const int len = static_cast<int>(data.size());
    int w = 0, h = 0, n = 0;
    if (stbi_info_from_memory(buf, len, &w, &h, &n)) check_dimensions(std::uint64_t(w), std::uint64_t(h));
    Raster r;
    if (stbi_is_16_bit_from_memory(buf, len)) {
        stbi_us* px = stbi_load_16_from_memory(buf, len, &w, &h, &n, 0);
        if (!px) fail(ErrorCode::UnsupportedFormat, std::string("No se puede decodificar la imagen: ") + stbi_failure_reason());
        r = Raster(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), static_cast<std::uint8_t>(n), 16);
        std::copy(px, px + r.samples.size(), r.samples.begin());
        stbi_image_free(px);
    } else {
        stbi_uc* px = stbi_load_from_memory(buf, len, &w, &h, &n, 0);
        if (!px) fail(ErrorCode::UnsupportedFormat, std::string("No se puede decodificar la imagen: ") + stbi_failure_reason());
        r = Raster(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), static_cast<std::uint8_t>(n), 8);
        std::copy(px, px + r.samples.size(), r.samples.begin());
        stbi_image_free(px);
    }
    return r;
}

void stb_sink(void* ctx, void* data, int size) {
    auto* out = static_cast<Bytes*>(ctx);
    const auto* p = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), p, p + size);
}

std::vector<std::uint8_t> to_u8(const Raster& r) {
    std::vector<std::uint8_t> v(r.samples.size());
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<std::uint8_t>(r.samples[i]);
    return v;
}

}  // namespace

Raster decode_raster(ByteSpan data, ImageFormat format) {
    switch (format) {
        case ImageFormat::Qoi: return qoi_decode(data);
        case ImageFormat::Pnm: return pnm_decode(data);
        case ImageFormat::Unknown: fail(ErrorCode::UnsupportedFormat, "Formato de imagen no reconocido");
        default: return decode_with_stb(data);
    }
}

Raster canonicalize_for(const Raster& in, ImageFormat format, std::vector<std::string>& notes) {
    Raster r = in;
    auto set_depth8 = [&] {
        if (r.depth != 8) {
            r = convert_depth(r, 8);
            notes.push_back(std::string("El formato ") + format_name(format) +
                            " solo admite 8 bits por muestra: se ha reducido la profundidad de 16 a 8 bits.");
        }
    };
    auto set_channels = [&](std::uint8_t ch, const char* why) {
        if (r.channels != ch) {
            r = convert_channels(r, ch);
            notes.push_back(std::string(format_name(format)) + ": " + why);
        }
    };

    switch (format) {
        case ImageFormat::Png: break;  // PNG representa cualquier combinacion
        case ImageFormat::Pnm:
            if (r.channels == 2) set_channels(1, "PGM no tiene canal alfa; se ha descartado la transparencia.");
            if (r.channels == 4) set_channels(3, "PPM no tiene canal alfa; se ha descartado la transparencia.");
            break;
        case ImageFormat::Qoi:
        case ImageFormat::Bmp:
        case ImageFormat::Tga:
            set_depth8();
            if (r.channels == 1) set_channels(3, "no se usa gris de 1 canal; se ha expandido a RGB.");
            if (r.channels == 2) set_channels(4, "no se usa gris+alfa; se ha expandido a RGBA.");
            break;
        default: fail(ErrorCode::UnsupportedFormat, std::string("No se puede escribir en formato ") + format_name(format));
    }
    return r;
}

Bytes encode_raster(const Raster& r, ImageFormat format, const std::vector<PngChunk>& png_extra) {
    Bytes out;
    switch (format) {
        case ImageFormat::Png: return png_encode(r, png_extra);
        case ImageFormat::Pnm: return pnm_encode(r);
        case ImageFormat::Qoi: return qoi_encode(r);
        case ImageFormat::Bmp: {
            const auto px = to_u8(r);
            if (!stbi_write_bmp_to_func(stb_sink, &out, static_cast<int>(r.width), static_cast<int>(r.height),
                                        r.channels, px.data()))
                fail(ErrorCode::Io, "Error codificando BMP");
            return out;
        }
        case ImageFormat::Tga: {
            const auto px = to_u8(r);
            if (!stbi_write_tga_to_func(stb_sink, &out, static_cast<int>(r.width), static_cast<int>(r.height),
                                        r.channels, px.data()))
                fail(ErrorCode::Io, "Error codificando TGA");
            return out;
        }
        default: fail(ErrorCode::UnsupportedFormat, std::string("No se puede escribir en formato ") + format_name(format));
    }
}

}  // namespace icpu::image
