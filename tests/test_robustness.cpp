// Entradas hostiles: limites de memoria y mutaciones de ficheros validos. Sirve en
// cualquier compilador; el fuzzing guiado por cobertura esta en fuzz/.

#include <doctest.h>

#include "helpers.hpp"
#include "icpu/compress/zlib.hpp"
#include "icpu/gif/gif.hpp"
#include "icpu/image/codec.hpp"
#include "icpu/image/format.hpp"
#include "icpu/image/limits.hpp"
#include "icpu/image/png.hpp"
#include "icpu/image/pnm.hpp"
#include "icpu/image/qoi.hpp"
#include "icpu/jpeg/jpeg.hpp"
#include "icpu/stego/payload.hpp"
#include "icpu/icpu.hpp"

using namespace icpu;

namespace {

// GIF minimo: declara width x height pero apenas trae datos.
Bytes gif_bomb(std::uint16_t width, std::uint16_t height) {
    Bytes g = {'G', 'I', 'F', '8', '9', 'a'};
    put_u16le(g, width);
    put_u16le(g, height);
    append(g, Bytes{0x80, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, ','});
    put_u16le(g, 0);
    put_u16le(g, 0);
    put_u16le(g, width);
    put_u16le(g, height);
    append(g, Bytes{0, 2, 2, 0x4C, 1, 0, ';'});
    return g;
}

// Decodificar no debe hacer nada peor que lanzar un Error de I-CPU.
void survive(ByteSpan data, std::string_view name) {
    try {
        inspect(data, name);
    } catch (const Error&) {
    }
    try {
        ExtractOptions o;
        o.password = "x";
        o.kdf = test::fast_kdf();
        extract(data, o, name);
    } catch (const Error&) {
    }
}

}  // namespace

TEST_SUITE("robustez") {

TEST_CASE("GIF: unas pocas decenas de bytes no pueden pedir gigabytes") {
    CHECK(test::error_of([] { gif::decode(gif_bomb(8000, 8000)); }).has_value());
    CHECK(test::error_of([] { gif::decode(gif_bomb(65535, 65535)); }).has_value());
    CHECK_FALSE(test::error_of([] { gif::decode(gif_bomb(4, 4)); }).has_value());
    // Un GIF real muy comprimible sigue siendo valido
    gif::Image flat = test::natural_gif(600, 600);
    std::fill(flat.frames[0].indices.begin(), flat.frames[0].indices.end(), std::uint8_t(7));
    CHECK_FALSE(test::error_of([&] { gif::decode(gif::encode(flat)); }).has_value());
}

TEST_CASE("Dimensiones: se rechazan antes de reservar memoria") {
    CHECK_FALSE(image::dimensions_allowed(0, 10));
    CHECK_FALSE(image::dimensions_allowed(100000, 100000));
    CHECK(image::dimensions_allowed(10000, 10000));
    CHECK(test::error_of([] { image::check_dimensions(70000, 70000); }) == ErrorCode::UnsupportedFormat);

    const std::string_view header = "P6 60000 60000 255 ";
    const ByteSpan pnm = as_bytes(header);
    CHECK(test::error_of([&] { image::pnm_decode(pnm); }) == ErrorCode::UnsupportedFormat);

    // PNG de 4 x 4 cuyo IHDR se reescribe a 60000 x 60000 (el ancho y el alto son big-endian)
    Bytes png = image::png_encode(test::natural_raster(4, 4));
    for (const std::size_t offset : {std::size_t(16), std::size_t(20)}) {
        png[offset + 2] = 0xEA;
        png[offset + 3] = 0x60;
    }
    CHECK(test::error_of([&] { image::decode_raster(png, image::ImageFormat::Png); }).has_value());
}

TEST_CASE("zlib: el tamano declarado limita la salida y hay un maximo") {
    const Bytes zeros(100000, 0);
    const Bytes z = compress::zlib_compress(zeros);
    CHECK(compress::zlib_decompress(z, zeros.size()) == zeros);
    CHECK(test::error_of([&] { compress::zlib_decompress(z, 1000); }) == ErrorCode::CorruptData);
    CHECK(test::error_of([&] { compress::zlib_decompress(z, 0xFFFFFFFFu); }) == ErrorCode::CorruptData);

    // Carga util autenticada pero que declara mas de lo permitido
    Bytes packed{0x01, 0};
    put_u32le(packed, 0xFFFFFFFFu);
    append(packed, z);
    CHECK(test::error_of([&] { stego::unpack_payload(packed); }) == ErrorCode::CorruptData);
}

TEST_CASE("Mutaciones: ningun decodificador falla con ficheros danados") {
    const Bytes png = image::png_encode(test::natural_raster(32, 24));
    const Bytes jpg = jpeg::compress_pixels(test::natural_raster(32, 24), 80);
    const Bytes gif_file = gif::encode(test::natural_gif(32, 24));
    const Bytes qoi = image::qoi_encode(test::natural_raster(32, 24));
    const Bytes pnm = image::pnm_encode(test::natural_raster(32, 24));

    test::Rng rng(42);
    const std::pair<const Bytes*, const char*> seeds[] = {
        {&png, "x.png"}, {&jpg, "x.jpg"}, {&gif_file, "x.gif"}, {&qoi, "x.qoi"}, {&pnm, "x.ppm"}};
    for (const auto& [seed, name] : seeds)
        for (int round = 0; round < 150; ++round) {
            Bytes mutated = *seed;
            const unsigned edits = 1 + rng.next() % 16;
            for (unsigned e = 0; e < edits; ++e)
                mutated[rng.next() % mutated.size()] = static_cast<std::uint8_t>(rng.next());
            if (round % 5 == 0) mutated.resize(1 + rng.next() % mutated.size());
            survive(mutated, name);
        }
}

}  // TEST_SUITE robustez
