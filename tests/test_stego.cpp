// stego: carga util, orden de ranuras, invariantes de las portadoras, motor
// y API de alto nivel (embed/extract/inspect) en todos los formatos.

#include <doctest.h>

#include <set>

#include "helpers.hpp"
#include "icpu/crypto/random.hpp"
#include "icpu/image/codec.hpp"
#include "icpu/image/png.hpp"
#include "icpu/jpeg/jpeg.hpp"
#include "icpu/stego/dct_carrier.hpp"
#include "icpu/stego/engine.hpp"
#include "icpu/stego/palette_carrier.hpp"
#include "icpu/stego/pixel_carrier.hpp"
#include "icpu/icpu.hpp"

using namespace icpu;
using image::ImageFormat;

namespace {

Bytes random_bytes(std::size_t n) {
    Bytes b(n);
    crypto::os_random(b.data(), b.size());
    return b;
}

std::string hex(const crypto::Digest256& d) { return to_hex(ByteSpan(d.data(), d.size())); }

Bytes png_cover(std::uint32_t w = 96, std::uint32_t h = 72, std::uint8_t channels = 3, std::uint8_t depth = 8) {
    return image::png_encode(test::natural_raster(w, h, channels, depth));
}

EmbedOptions fast_options(std::string password = "clave") {
    EmbedOptions o;
    o.password = std::move(password);
    o.kdf = test::fast_kdf();
    return o;
}

ExtractOptions fast_extract(std::string password = "clave", Layout layout = Layout::Random) {
    ExtractOptions o;
    o.password = std::move(password);
    o.layout = layout;
    o.kdf = test::fast_kdf();
    return o;
}

// Comprueba las invariantes de Carrier: insertar no cambia la sal ni el conjunto
// de ranuras utilizables, y el mensaje se recupera; otra contrasena falla.
void check_carrier(stego::Carrier& carrier, unsigned k, stego::EmbedMode mode, stego::Layout layout) {
    const auto salt_before = hex(stego::carrier_salt(carrier));
    const auto usable_before = carrier.usable_count();
    std::vector<bool> usable_map;
    for (std::uint64_t s = 0; s < carrier.slot_count(); ++s) usable_map.push_back(carrier.usable(s));

    const std::size_t cap = stego::max_payload_bytes(carrier, k);
    REQUIRE(cap > 0);
    const Bytes payload = random_bytes(cap);  // a capacidad completa
    stego::EngineParams p;
    p.bits_per_slot = k;
    p.mode = mode;
    p.layout = layout;
    p.kdf = test::fast_kdf();
    const auto stats = stego::embed(carrier, "pw", payload, p);
    CHECK(stats.slots_used > 0);
    CHECK(stats.slots_changed <= stats.slots_used);

    CHECK(hex(stego::carrier_salt(carrier)) == salt_before);
    CHECK(carrier.usable_count() == usable_before);
    for (std::uint64_t s = 0; s < carrier.slot_count(); ++s) REQUIRE(carrier.usable(s) == usable_map[s]);

    CHECK(stego::extract(carrier, "pw", layout, test::fast_kdf()) == payload);
    CHECK(test::error_of([&] { stego::extract(carrier, "otra", layout, test::fast_kdf()); }) ==
          ErrorCode::Authentication);

    // Un byte mas de la capacidad no cabe
    CHECK(test::error_of([&] { stego::embed(carrier, "pw", random_bytes(cap + 1), p); }) == ErrorCode::Capacity);
}

}  // namespace

TEST_SUITE("stego") {

TEST_CASE("Payload: texto, fichero con nombre y compresion opcional") {
    std::string long_text;
    for (int i = 0; i < 100; ++i) long_text += "texto muy repetitivo ";
    const auto packed = stego::pack_payload(stego::Payload::text(long_text));
    CHECK(packed.compressed);
    CHECK(packed.bytes.size() < long_text.size() / 4);
    const auto back = stego::unpack_payload(packed.bytes);
    CHECK(back.kind == stego::Payload::Kind::Text);
    CHECK(back.as_text() == long_text);

    const Bytes noise = random_bytes(500);
    const auto file = stego::Payload::file("datos ñ.bin", noise);
    const auto packed_file = stego::pack_payload(file);
    CHECK_FALSE(packed_file.compressed);  // los datos aleatorios no se comprimen
    CHECK(packed_file.bytes.size() == stego::packed_size_uncompressed(file));
    const auto back_file = stego::unpack_payload(packed_file.bytes);
    CHECK(back_file.kind == stego::Payload::Kind::File);
    CHECK(back_file.name == "datos ñ.bin");
    CHECK(back_file.data == noise);

    CHECK_FALSE(stego::pack_payload(stego::Payload::text(long_text), false).compressed);
    CHECK(stego::unpack_payload(stego::pack_payload(stego::Payload::text("")).bytes).data.empty());
    CHECK(test::error_of([] { stego::unpack_payload(Bytes{0xFF}); }).has_value());
}

TEST_CASE("Orden de ranuras: permutacion completa, determinista y dependiente de la clave") {
    crypto::Key256 k1, k2;
    k1.data()[0] = 1;
    k2.data()[0] = 2;
    const std::uint64_t n = 5000;  // pasa de representacion dispersa a densa a mitad

    auto take = [&](const crypto::Key256& key, std::uint64_t count) {
        stego::RandomOrder o(n, key);
        std::vector<std::uint64_t> v;
        for (std::uint64_t i = 0; i < count; ++i) v.push_back(o.next());
        return v;
    };
    const auto full = take(k1, n);
    CHECK(std::set<std::uint64_t>(full.begin(), full.end()).size() == n);
    CHECK(*std::max_element(full.begin(), full.end()) == n - 1);
    CHECK(take(k1, n) == full);
    const auto prefix = take(k1, 100);  // solo representacion dispersa
    CHECK(std::equal(prefix.begin(), prefix.end(), full.begin()));
    CHECK(take(k2, 100) != prefix);

    stego::RandomOrder exhausted(3, k1);
    for (int i = 0; i < 3; ++i) exhausted.next();
    CHECK(test::error_of([&] { exhausted.next(); }) == ErrorCode::Capacity);

    stego::SequentialOrder seq(3);
    CHECK(seq.next() == 0);
    CHECK(seq.next() == 1);
    CHECK(seq.next() == 2);
    CHECK(test::error_of([&] { seq.next(); }) == ErrorCode::Capacity);
}

TEST_CASE("PixelCarrier: invariantes con k = 1..4, matching y orden secuencial") {
    for (unsigned k = 1; k <= 4; ++k) {
        CAPTURE(k);
        image::Raster r = test::natural_raster(40, 30);
        stego::PixelCarrier c(r);
        check_carrier(c, k, stego::EmbedMode::Replace, stego::Layout::Random);
    }
    {
        image::Raster r = test::natural_raster(40, 30);
        stego::PixelCarrier c(r);
        check_carrier(c, 1, stego::EmbedMode::Match, stego::Layout::Random);
    }
    {
        image::Raster r = test::natural_raster(40, 30, 3, 16);
        stego::PixelCarrier c(r);
        check_carrier(c, 2, stego::EmbedMode::Replace, stego::Layout::Sequential);
    }
}

TEST_CASE("PixelCarrier: el canal alfa no se toca y matching cambia como mucho +-1") {
    const image::Raster original = test::natural_raster(40, 30, 4);
    image::Raster r = original;
    stego::PixelCarrier c(r);
    CHECK(c.slot_count() == 40u * 30u * 3u);
    stego::EngineParams p;
    p.mode = stego::EmbedMode::Match;
    p.kdf = test::fast_kdf();
    stego::embed(c, "pw", random_bytes(stego::max_payload_bytes(c, 1)), p);
    for (std::size_t px = 0; px < r.pixel_count(); ++px) {
        REQUIRE(r.at(px, 3) == original.at(px, 3));
        for (unsigned ch = 0; ch < 3; ++ch) REQUIRE(std::abs(int(r.at(px, ch)) - int(original.at(px, ch))) <= 1);
    }
}

TEST_CASE("DctCarrier: invariantes sobre un JPEG real") {
    const Bytes file = jpeg::compress_pixels(test::natural_raster(96, 72), 90);
    jpeg::CoefImage coefs = jpeg::read_coefficients(file);
    stego::DctCarrier c(coefs);
    CHECK(c.max_bits_per_slot() == 1);
    CHECK_FALSE(c.supports_matching());
    CHECK(stego::DctCarrier::usable_value(5, 3));
    CHECK_FALSE(stego::DctCarrier::usable_value(5, 0));  // DC
    CHECK_FALSE(stego::DctCarrier::usable_value(0, 3));
    CHECK_FALSE(stego::DctCarrier::usable_value(1, 3));
    check_carrier(c, 1, stego::EmbedMode::Replace, stego::Layout::Random);
}

TEST_CASE("PaletteCarrier: invariantes y ordenacion EzStego") {
    gif::Image img = test::natural_gif(60, 50);
    const auto order = stego::PaletteCarrier::sort_palette(img.global_palette);
    CHECK(std::set<std::uint8_t>(order.begin(), order.end()).size() == 256);
    stego::PaletteCarrier c(img);
    check_carrier(c, 1, stego::EmbedMode::Replace, stego::Layout::Random);
}

TEST_CASE("API: embed/extract en todos los formatos de salida") {
    const Bytes cover = png_cover(256, 192);  // la imagen sintetica es suave: pocos coeficientes AC utiles en JPEG
    const std::string msg = "Mensaje secreto — con acentos: ñandú, 日本";
    for (const ImageFormat out :
         {ImageFormat::Png, ImageFormat::Bmp, ImageFormat::Tga, ImageFormat::Pnm, ImageFormat::Qoi, ImageFormat::Jpeg}) {
        CAPTURE(image::format_name(out));
        EmbedOptions o = fast_options();
        o.output_format = out;
        const EmbedResult r = embed(cover, Payload::text(msg), o, "cover.png");
        CHECK(r.verified);
        CHECK(r.format == out);
        CHECK(image::detect_format(r.image) == out);
        CHECK(r.domain == image::domain_for_output(out));
        CHECK(extract(r.image, fast_extract(), std::string("s.") + image::default_extension(out)).as_text() == msg);
    }

    // GIF: requiere portadora GIF
    const Bytes gif_cover = gif::encode(test::natural_gif(80, 60));
    const EmbedResult g = embed(gif_cover, Payload::text(msg), fast_options(), "c.gif");
    CHECK(g.format == ImageFormat::Gif);
    CHECK(extract(g.image, fast_extract(), "s.gif").as_text() == msg);
    EmbedOptions to_gif = fast_options();
    to_gif.output_format = ImageFormat::Gif;
    CHECK(test::error_of([&] { embed(cover, Payload::text(msg), to_gif, "c.png"); }) == ErrorCode::UnsupportedFormat);

    // JPEG -> JPEG: se trabaja sobre los coeficientes sin recomprimir
    const Bytes jpg = jpeg::compress_pixels(test::natural_raster(256, 192), 80);
    const EmbedResult j = embed(jpg, Payload::text("dct"), fast_options(), "c.jpg");
    CHECK(j.format == ImageFormat::Jpeg);
    CHECK(jpeg::estimate_quality(jpeg::read_coefficients(j.image)) == jpeg::estimate_quality(jpeg::read_coefficients(jpg)));
    CHECK(extract(j.image, fast_extract(), "s.jpg").as_text() == "dct");
}

TEST_CASE("API: eleccion automatica de k, 16 bits y alfa") {
    // Mensaje que no cabe con k = 1 pero si con k = 2
    const Bytes cover = png_cover(64, 48);
    const ImageInfo info = inspect(cover, "c.png");
    std::size_t cap1 = 0;
    for (const auto& c : info.capacities)
        if (c.output == ImageFormat::Png && c.bits_per_slot == 1) cap1 = c.message_bytes;
    REQUIRE(cap1 > 0);
    EmbedOptions o = fast_options();
    o.compress = false;
    const Bytes data = random_bytes(cap1 + 100);
    const EmbedResult r = embed(cover, Payload::file("x.bin", data), o, "c.png");
    CHECK(r.bits_per_slot == 2);
    CHECK(extract(r.image, fast_extract(), "s.png").data == data);

    // Exactamente la capacidad anunciada con k = 1 cabe con k = 1
    const EmbedResult exact = embed(cover, Payload::text(std::string(cap1, 'x')), o, "c.png");
    CHECK(exact.bits_per_slot == 1);

    // 16 bits y alfa se conservan en PNG
    const Bytes cover16 = png_cover(40, 30, 4, 16);
    const EmbedResult r16 = embed(cover16, Payload::text("16 bits"), fast_options(), "c.png");
    const image::Raster before = image::decode_raster(cover16, ImageFormat::Png);
    const image::Raster after = image::decode_raster(r16.image, ImageFormat::Png);
    CHECK(after.depth == 16);
    CHECK(after.channels == 4);
    for (std::size_t px = 0; px < after.pixel_count(); ++px) REQUIRE(after.at(px, 3) == before.at(px, 3));
    CHECK(extract(r16.image, fast_extract(), "s.png").as_text() == "16 bits");

    // 16 bits -> BMP: se reduce a 8 bits y se avisa
    EmbedOptions to_bmp = fast_options();
    to_bmp.output_format = ImageFormat::Bmp;
    const EmbedResult rb = embed(cover16, Payload::text("bmp"), to_bmp, "c.png");
    CHECK_FALSE(rb.notes.empty());
    CHECK(extract(rb.image, fast_extract(), "s.bmp").as_text() == "bmp");
}

TEST_CASE("API: errores de autenticacion, orden, manipulacion y capacidad") {
    const Bytes cover = png_cover();
    const EmbedResult r = embed(cover, Payload::text("hola"), fast_options(), "c.png");

    CHECK(test::error_of([&] { extract(r.image, fast_extract("otra"), "s.png"); }) == ErrorCode::Authentication);
    CHECK(test::error_of([&] { extract(cover, fast_extract(), "c.png"); }) == ErrorCode::Authentication);
    CHECK(test::error_of([&] { extract(r.image, fast_extract("clave", Layout::Sequential), "s.png"); }) ==
          ErrorCode::Authentication);

    // Invertir todos los LSB: la sal (bits altos) no cambia, pero el AEAD lo detecta
    image::Raster px = image::decode_raster(r.image, ImageFormat::Png);
    for (auto& s : px.samples) s ^= 1;
    const Bytes tampered = image::png_encode(px);
    CHECK(test::error_of([&] { extract(tampered, fast_extract(), "t.png"); }) == ErrorCode::Authentication);

    // Orden secuencial: solo se extrae con el mismo orden
    EmbedOptions seq = fast_options();
    seq.layout = Layout::Sequential;
    const EmbedResult rs = embed(cover, Payload::text("seq"), seq, "c.png");
    CHECK(extract(rs.image, fast_extract("clave", Layout::Sequential), "s.png").as_text() == "seq");

    // Capacidad y parametros invalidos
    EmbedOptions o = fast_options();
    o.compress = false;
    CHECK(test::error_of([&] { embed(cover, Payload::file("big", random_bytes(100000)), o, "c.png"); }) ==
          ErrorCode::Capacity);
    CHECK(test::error_of([&] { embed(cover, Payload::text("x"), fast_options(""), "c.png"); }) ==
          ErrorCode::InvalidArgument);
    CHECK(test::error_of([&] { embed(as_bytes("no es una imagen"), Payload::text("x"), fast_options(), "x.txt"); }) ==
          ErrorCode::UnsupportedFormat);
}

TEST_CASE("API: inspect informa dimensiones y capacidades por dominio") {
    const ImageInfo png = inspect(png_cover(64, 48), "c.png");
    CHECK(png.format == ImageFormat::Png);
    CHECK(png.width == 64);
    CHECK(png.height == 48);
    bool has_spatial = false, has_dct = false;
    for (const auto& c : png.capacities) {
        has_spatial |= c.domain == image::Domain::Spatial;
        has_dct |= c.domain == image::Domain::Dct;
        if (c.domain == image::Domain::Spatial) CHECK(c.usable_slots == 64u * 48u * 3u);
    }
    CHECK(has_spatial);
    CHECK(has_dct);

    const ImageInfo jpg = inspect(jpeg::compress_pixels(test::natural_raster(64, 48), 75), "c.jpg");
    CHECK(std::abs(jpg.jpeg_quality - 75) <= 2);
    CHECK(jpg.jpeg_subsampling == "4:2:0");

    const ImageInfo g = inspect(gif::encode(test::natural_gif(30, 20)), "c.gif");
    CHECK(g.frames == 1);
    CHECK(g.capacities.front().domain == image::Domain::Palette);
}

}  // TEST_SUITE stego

TEST_SUITE("formato") {

namespace {

// Los 30 bytes de cabecera tal como quedan en la imagen (orden secuencial = ranuras 0..239).
Bytes raw_header(const stego::Carrier& c) {
    Bytes out(stego::kHeaderBytes, 0);
    for (std::size_t i = 0; i < stego::kHeaderSlots; ++i)
        out[i / 8] = static_cast<std::uint8_t>(out[i / 8] | (c.read(i, 1) << (7 - i % 8)));
    return out;
}

Bytes embed_sequential(image::Raster& r, std::uint8_t version, const Bytes& payload) {
    stego::PixelCarrier c(r);
    stego::EngineParams p;
    p.layout = stego::Layout::Sequential;
    p.kdf = test::fast_kdf();
    p.format_version = version;
    stego::embed(c, "pw", payload, p);
    return raw_header(c);
}

}  // namespace

TEST_CASE("v2 se escribe por defecto y v1 se sigue leyendo") {
    CHECK(stego::kFormatVersion == 2);
    const Bytes payload = random_bytes(200);
    for (const std::uint8_t version : {std::uint8_t(1), std::uint8_t(2)}) {
        CAPTURE(int(version));
        for (const auto layout : {stego::Layout::Random, stego::Layout::Sequential}) {
            image::Raster r = test::natural_raster(48, 40);
            stego::PixelCarrier c(r);
            stego::EngineParams p;
            p.layout = layout;
            p.kdf = test::fast_kdf();
            p.format_version = version;
            stego::embed(c, "pw", payload, p);
            CHECK(stego::extract(c, "pw", layout, test::fast_kdf()) == payload);
            CHECK(test::error_of([&] { stego::extract(c, "otra", layout, test::fast_kdf()); }) ==
                  ErrorCode::Authentication);
        }
    }
    image::Raster r = test::natural_raster(16, 16);
    stego::PixelCarrier c(r);
    stego::EngineParams bad;
    bad.format_version = 3;
    CHECK(test::error_of([&] { stego::embed(c, "pw", random_bytes(4), bad); }) == ErrorCode::InvalidArgument);
}

TEST_CASE("v2: la cabecera no deja nada reconocible, ni en orden secuencial") {
    const Bytes payload = random_bytes(100);
    // v1: version, params y longitud en claro -> identicos en dos inserciones
    image::Raster a1 = test::natural_raster(48, 40), b1 = test::natural_raster(48, 40);
    const Bytes h1a = embed_sequential(a1, 1, payload), h1b = embed_sequential(b1, 1, payload);
    CHECK(h1a[0] == 1);
    CHECK(h1a[1] == h1b[1]);
    CHECK(std::equal(h1a.begin() + 26, h1a.end(), h1b.begin() + 26));

    // v2: todo depende del nonce aleatorio -> los 30 bytes cambian en cada insercion
    image::Raster a2 = test::natural_raster(48, 40), b2 = test::natural_raster(48, 40);
    const Bytes h2a = embed_sequential(a2, 2, payload), h2b = embed_sequential(b2, 2, payload);
    CHECK_FALSE(std::equal(h2a.begin() + 24, h2a.end(), h2b.begin() + 24));
    CHECK_FALSE(std::equal(h2a.begin(), h2a.begin() + 24, h2b.begin()));
}

TEST_CASE("v2: alterar un bit de la cabecera cifrada se detecta") {
    const Bytes payload = random_bytes(64);
    for (std::size_t bit : {std::size_t(0), std::size_t(200), std::size_t(239)}) {  // nonce y zona cifrada
        CAPTURE(bit);
        image::Raster r = test::natural_raster(48, 40);
        embed_sequential(r, 2, payload);
        stego::PixelCarrier c(r);
        REQUIRE(stego::extract(c, "pw", stego::Layout::Sequential, test::fast_kdf()) == payload);
        crypto::RandomBits rng;
        c.write(bit, c.read(bit, 1) ^ 1u, 1, stego::EmbedMode::Replace, rng);
        CHECK(test::error_of([&] { stego::extract(c, "pw", stego::Layout::Sequential, test::fast_kdf()); }) ==
              ErrorCode::Authentication);
    }
}

}  // TEST_SUITE formato
