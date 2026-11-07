#include "icpu/stego/document.hpp"

#include "icpu/core/error.hpp"
#include "icpu/gif/gif.hpp"
#include "icpu/image/codec.hpp"
#include "icpu/image/png.hpp"
#include "icpu/jpeg/jpeg.hpp"
#include "icpu/stego/dct_carrier.hpp"
#include "icpu/stego/palette_carrier.hpp"
#include "icpu/stego/pixel_carrier.hpp"

namespace icpu::stego {

namespace {

using image::Domain;
using image::ImageFormat;

class SpatialDocument final : public Document {
public:
    SpatialDocument(image::Raster raster, ImageFormat format, std::vector<image::PngChunk> png_chunks)
        : raster_(std::move(raster)), format_(format), png_chunks_(std::move(png_chunks)), carrier_(raster_) {}

    Carrier& carrier() override { return carrier_; }
    Bytes serialize() const override { return image::encode_raster(raster_, format_, png_chunks_); }
    ImageFormat format() const override { return format_; }
    Domain domain() const override { return Domain::Spatial; }

private:
    image::Raster raster_;  // debe declararse antes que carrier_ (orden de construccion)
    ImageFormat format_;
    std::vector<image::PngChunk> png_chunks_;
    PixelCarrier carrier_;
};

class JpegDocument final : public Document {
public:
    explicit JpegDocument(Bytes original)
        : original_(std::move(original)), coefs_(jpeg::read_coefficients(original_)), carrier_(coefs_) {}

    Carrier& carrier() override { return carrier_; }
    Bytes serialize() const override { return jpeg::write_coefficients(original_, coefs_); }
    ImageFormat format() const override { return ImageFormat::Jpeg; }
    Domain domain() const override { return Domain::Dct; }
    const jpeg::CoefImage& coefficients() const { return coefs_; }

private:
    Bytes original_;
    jpeg::CoefImage coefs_;
    DctCarrier carrier_;
};

class GifDocument final : public Document {
public:
    explicit GifDocument(gif::Image image) : image_(std::move(image)), carrier_(image_) {}

    Carrier& carrier() override { return carrier_; }
    Bytes serialize() const override { return gif::encode(image_); }
    ImageFormat format() const override { return ImageFormat::Gif; }
    Domain domain() const override { return Domain::Palette; }

private:
    gif::Image image_;
    PaletteCarrier carrier_;
};

// Decodifica cualquier formato legible a pixeles.
image::Raster decode_any(ByteSpan data, ImageFormat source, std::vector<std::string>& notes) {
    if (source == ImageFormat::Jpeg) return jpeg::decompress_pixels(data);
    if (source == ImageFormat::Gif) {
        const gif::Image g = gif::decode(data);
        if (g.frames.size() > 1)
            notes.push_back("El GIF es animado: en un formato de pixeles solo se conserva el primer fotograma "
                            "(guardalo como .gif para conservar la animacion).");
        return gif::first_frame_rgba(g);
    }
    if (source == ImageFormat::Png)
        for (const auto& chunk : image::png_read_metadata(data).ancillary)
            if (chunk.type == "acTL") notes.push_back("El PNG es animado: solo se conserva el primer fotograma.");
    return image::decode_raster(data, source);
}

}  // namespace

std::unique_ptr<Document> open_cover(ByteSpan cover, ImageFormat source, ImageFormat output, int jpeg_quality) {
    require(source != ImageFormat::Unknown, ErrorCode::UnsupportedFormat, "Formato de la portadora no reconocido");
    require(image::can_write(output), ErrorCode::UnsupportedFormat, "Formato de salida no soportado");
    std::vector<std::string> notes;
    std::unique_ptr<Document> doc;

    switch (image::domain_for_output(output)) {
        case Domain::Dct: {
            if (source == ImageFormat::Jpeg) {
                notes.push_back("Portadora JPEG: se modifican directamente sus coeficientes DCT, sin "
                                "recomprimir (no se pierde calidad adicional).");
                doc = std::make_unique<JpegDocument>(Bytes(cover.begin(), cover.end()));
            } else {
                image::Raster r = decode_any(cover, source, notes);
                r = image::convert_depth(r, 8);
                if (r.has_alpha()) {
                    notes.push_back("JPEG no admite transparencia: se ha descartado el canal alfa.");
                    r = image::convert_channels(r, r.channels == 2 ? 1 : 3);
                }
                notes.push_back("La imagen se ha comprimido a JPEG (calidad " + std::to_string(jpeg_quality) +
                                ") y despues se han ocultado los datos en sus coeficientes DCT.");
                doc = std::make_unique<JpegDocument>(jpeg::compress_pixels(r, jpeg_quality));
            }
            break;
        }
        case Domain::Palette: {
            if (source != ImageFormat::Gif)
                fail(ErrorCode::UnsupportedFormat,
                     "La salida GIF requiere una portadora GIF (dominio de paleta). Usa PNG para imagenes "
                     "de color verdadero.");
            doc = std::make_unique<GifDocument>(gif::decode(cover));
            break;
        }
        case Domain::Spatial: {
            image::Raster r = decode_any(cover, source, notes);
            if (image::is_lossy(source))
                notes.push_back(std::string("La portadora ") + image::format_name(source) +
                                " se guarda en un formato sin perdida: el fichero resultante sera mayor.");
            r = image::canonicalize_for(r, output, notes);
            std::vector<image::PngChunk> chunks;
            if (source == ImageFormat::Png && output == ImageFormat::Png)
                chunks = image::png_chunks_to_preserve(image::png_read_metadata(cover), r);
            doc = std::make_unique<SpatialDocument>(std::move(r), output, std::move(chunks));
            break;
        }
    }
    doc->notes = std::move(notes);
    return doc;
}

std::unique_ptr<Document> open_stego(ByteSpan stego, std::string_view name_hint) {
    const ImageFormat format = image::detect_format(stego, name_hint);
    switch (format) {
        case ImageFormat::Unknown: fail(ErrorCode::UnsupportedFormat, "Formato de imagen no reconocido");
        case ImageFormat::Jpeg: return std::make_unique<JpegDocument>(Bytes(stego.begin(), stego.end()));
        case ImageFormat::Gif: return std::make_unique<GifDocument>(gif::decode(stego));
        default:
            return std::make_unique<SpatialDocument>(image::decode_raster(stego, format), format,
                                                     std::vector<image::PngChunk>{});
    }
}

}  // namespace icpu::stego
