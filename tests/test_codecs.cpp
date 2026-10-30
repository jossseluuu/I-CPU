// Codecs: DEFLATE, PNG/BMP/TGA/PNM/QOI ida y vuelta, deteccion de formato,
// coeficientes DCT de JPEG y GIF/LZW.

#include <doctest.h>

#include "helpers.hpp"
#include "icpu/compress/zlib.hpp"
#include "icpu/gif/gif.hpp"
#include "icpu/image/codec.hpp"
#include "icpu/image/format.hpp"
#include "icpu/image/png.hpp"
#include "icpu/jpeg/jpeg.hpp"

using namespace icpu;
using image::ImageFormat;
using image::Raster;

namespace {

bool same_pixels(const Raster& a, const Raster& b) {
    return a.width == b.width && a.height == b.height && a.channels == b.channels && a.depth == b.depth &&
           a.samples == b.samples;
}

}  // namespace

TEST_SUITE("codecs") {

TEST_CASE("zlib: ida y vuelta, compresion efectiva y tamano esperado") {
    Bytes text;
    for (int i = 0; i < 200; ++i) append(text, as_bytes("la misma frase una y otra vez. "));
    const Bytes z = compress::zlib_compress(text);
    CHECK(z.size() < text.size() / 10);
    CHECK(compress::zlib_decompress(z, text.size()) == text);
    CHECK(test::error_of([&] { compress::zlib_decompress(z, text.size() - 1); }).has_value());

    Bytes corrupt = z;
    corrupt[corrupt.size() / 2] ^= 0xFF;
    CHECK(test::error_of([&] { compress::zlib_decompress(corrupt, text.size()); }).has_value());
}

TEST_CASE("CRC-32 (PNG): vector de comprobacion estandar") {
    CHECK(image::crc32(as_bytes("123456789")) == 0xCBF43926u);
}

TEST_CASE("Codecs espaciales: ida y vuelta exacta tras canonicalizar") {
    const struct {
        ImageFormat fmt;
        std::uint8_t channels, depth;
    } cases[] = {
        {ImageFormat::Png, 1, 8},  {ImageFormat::Png, 2, 8},  {ImageFormat::Png, 3, 8},  {ImageFormat::Png, 4, 8},
        {ImageFormat::Png, 3, 16}, {ImageFormat::Png, 4, 16}, {ImageFormat::Png, 1, 16}, {ImageFormat::Bmp, 3, 8},
        {ImageFormat::Bmp, 4, 8},  {ImageFormat::Bmp, 1, 8},  {ImageFormat::Tga, 3, 8},  {ImageFormat::Tga, 4, 8},
        {ImageFormat::Pnm, 1, 8},  {ImageFormat::Pnm, 3, 8},  {ImageFormat::Pnm, 3, 16}, {ImageFormat::Pnm, 4, 8},
        {ImageFormat::Qoi, 3, 8},  {ImageFormat::Qoi, 4, 8},  {ImageFormat::Qoi, 3, 16},
    };
    for (const auto& c : cases) {
        CAPTURE(image::format_name(c.fmt));
        CAPTURE(int(c.channels));
        CAPTURE(int(c.depth));
        const Raster src = test::natural_raster(37, 23, c.channels, c.depth);  // dimensiones impares a proposito
        std::vector<std::string> notes;
        const Raster canon = image::canonicalize_for(src, c.fmt, notes);
        const Bytes file = image::encode_raster(canon, c.fmt);
        CHECK(image::detect_format(file) == c.fmt);
        CHECK(same_pixels(image::decode_raster(file, c.fmt), canon));
    }
}

TEST_CASE("Canonicalizar explica los cambios") {
    std::vector<std::string> notes;
    const Raster r = image::canonicalize_for(test::natural_raster(8, 8, 2, 16), ImageFormat::Bmp, notes);
    CHECK(r.depth == 8);
    CHECK(r.channels == 4);
    CHECK(notes.size() == 2);
    notes.clear();
    image::canonicalize_for(test::natural_raster(8, 8, 3, 16), ImageFormat::Png, notes);
    CHECK(notes.empty());
    CHECK(test::error_of([&] { image::canonicalize_for(test::natural_raster(8, 8), ImageFormat::Jpeg, notes); }) ==
          ErrorCode::UnsupportedFormat);
}

TEST_CASE("PNG: conserva los chunks auxiliares del original") {
    const Raster r = test::natural_raster(16, 16);
    const std::vector<image::PngChunk> extra{{"tEXt", Bytes{'C', 'o', 'm', 'm', 'e', 'n', 't', 0, 'h', 'i'}}};
    const Bytes file = image::png_encode(r, extra);
    const image::PngMetadata meta = image::png_read_metadata(file);
    CHECK(meta.color_type == 2);
    CHECK(meta.bit_depth == 8);
    REQUIRE(meta.ancillary.size() == 1);
    CHECK(meta.ancillary[0].type == "tEXt");
    CHECK(meta.ancillary[0].data == extra[0].data);
}

TEST_CASE("Deteccion de formato por numero magico y extension") {
    CHECK(image::detect_format(as_bytes("GIF89a......")) == ImageFormat::Gif);
    CHECK(image::detect_format(Bytes{0xFF, 0xD8, 0xFF, 0xE0}) == ImageFormat::Jpeg);
    CHECK(image::detect_format(as_bytes("P6\n1 1\n255\n")) == ImageFormat::Pnm);
    CHECK(image::detect_format(as_bytes("nada reconocible")) == ImageFormat::Unknown);
    CHECK(image::format_from_extension("JPG") == ImageFormat::Jpeg);
    CHECK(image::format_from_extension("jpeg") == ImageFormat::Jpeg);
    CHECK(image::format_from_extension("pgm") == ImageFormat::Pnm);
    CHECK(image::domain_for_output(ImageFormat::Gif) == image::Domain::Palette);
    CHECK(image::is_lossy(ImageFormat::Jpeg));
    CHECK_FALSE(image::can_write(ImageFormat::Psd));
}

TEST_CASE("JPEG: los coeficientes DCT sobreviven exactos a reescribir el fichero") {
    for (const std::uint8_t channels : {std::uint8_t(1), std::uint8_t(3)}) {
        CAPTURE(int(channels));
        const Bytes original = jpeg::compress_pixels(test::natural_raster(64, 48, channels), 85);
        CHECK(image::detect_format(original) == ImageFormat::Jpeg);
        jpeg::CoefImage coefs = jpeg::read_coefficients(original);
        CHECK(coefs.width == 64);
        CHECK(coefs.height == 48);
        CHECK(coefs.components.size() == channels);
        CHECK(std::abs(jpeg::estimate_quality(coefs) - 85) <= 2);

        // Cambiar el LSB de varios coeficientes AC y comprobar que se conservan
        auto& comp = coefs.components[0];
        int touched = 0;
        for (std::size_t i = 0; i < comp.coefs.size() && touched < 50; ++i)
            if (i % 64 != 0 && std::abs(comp.coefs[i]) >= 2) {
                comp.coefs[i] = static_cast<std::int16_t>(comp.coefs[i] ^ 1);
                ++touched;
            }
        REQUIRE(touched == 50);
        const Bytes rewritten = jpeg::write_coefficients(original, coefs);
        const jpeg::CoefImage back = jpeg::read_coefficients(rewritten);
        REQUIRE(back.components.size() == coefs.components.size());
        for (std::size_t c = 0; c < back.components.size(); ++c)
            CHECK(back.components[c].coefs == coefs.components[c].coefs);

        const Raster px = jpeg::decompress_pixels(rewritten);
        CHECK(px.width == 64);
        CHECK(px.channels == channels);
    }
}

TEST_CASE("JPEG corrupto: error controlado, sin abortar") {
    // libjpeg tolera datos de entropia truncados (rellena con ceros); lo importante
    // es que una cabecera rota se traduzca en Error y no en un abort/longjmp perdido.
    Bytes header_broken = jpeg::compress_pixels(test::natural_raster(32, 32), 90);
    header_broken.resize(20);
    CHECK(test::error_of([&] { jpeg::read_coefficients(header_broken); }).has_value());
    CHECK(test::error_of([&] { jpeg::write_coefficients(header_broken, {}); }).has_value());
    CHECK(test::error_of([&] { jpeg::decompress_pixels(header_broken); }).has_value());
}

TEST_CASE("LZW de GIF: ida y vuelta con distintos tamanos de codigo") {
    test::Rng rng(5);
    for (const std::uint8_t min_code : {std::uint8_t(2), std::uint8_t(4), std::uint8_t(8)}) {
        CAPTURE(int(min_code));
        std::vector<std::uint8_t> idx(20000);
        for (std::size_t i = 0; i < idx.size(); ++i)
            idx[i] = static_cast<std::uint8_t>((i % 300 < 150 ? std::size_t(rng.next()) : i / 97) % (std::size_t(1) << min_code));
        const Bytes z = gif::lzw_encode(idx, min_code);
        CHECK(gif::lzw_decode(z, min_code, idx.size()) == idx);
    }
}

TEST_CASE("GIF: decodificar(codificar(x)) conserva estructura e indices") {
    const gif::Image img = test::natural_gif(50, 40);
    const Bytes file = gif::encode(img);
    CHECK(image::detect_format(file) == ImageFormat::Gif);
    const gif::Image back = gif::decode(file);
    CHECK(back.width() == 50);
    CHECK(back.height() == 40);
    CHECK(back.global_palette == img.global_palette);
    REQUIRE(back.frames.size() == 1);
    CHECK(back.frames[0].indices == img.frames[0].indices);
    CHECK(gif::encode(back) == file);

    const Raster rgba = gif::first_frame_rgba(back);
    CHECK(rgba.channels == 4);
    const std::uint8_t i0 = img.frames[0].indices[0];
    CHECK(rgba.at(0, 0) == img.global_palette[i0 * 3u]);
}

}  // TEST_SUITE codecs
