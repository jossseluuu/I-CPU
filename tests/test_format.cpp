// Formato v3: STC, sal almacenada, huellas estadisticas corregidas y lectura de
// imagenes reales de cada version (tests/golden).

#include <doctest.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <string>

#include "helpers.hpp"
#include "icpu/crypto/random.hpp"
#include "icpu/image/codec.hpp"
#include "icpu/image/png.hpp"
#include "icpu/jpeg/jpeg.hpp"
#include "icpu/stego/dct_carrier.hpp"
#include "icpu/stego/engine.hpp"
#include "icpu/stego/palette_carrier.hpp"
#include "icpu/stego/pixel_carrier.hpp"
#include "icpu/stego/stc.hpp"
#include "icpu/icpu.hpp"

using namespace icpu;

namespace {

constexpr char kDemoMessage[] = "Hola! Si lees esto, has extraido correctamente el mensaje de la demo de I-CPU.";

Bytes golden(const std::string& name) {
    std::ifstream in(std::string(ICPU_GOLDEN_DIR) + "/" + name, std::ios::binary);
    REQUIRE(in.good());
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Bytes random_bytes(std::size_t n) {
    Bytes b(n);
    crypto::os_random(b.data(), b.size());
    return b;
}

std::vector<std::uint8_t> random_bits(test::Rng& rng, std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (auto& b : v) b = rng.next() & 1u;
    return v;
}

stego::EngineParams params(EmbedMode mode, Layout layout = Layout::Random) {
    stego::EngineParams p;
    p.mode = mode;
    p.layout = layout;
    p.kdf = test::fast_kdf();
    return p;
}

// Ranuras en las que difieren dos imagenes.
std::set<std::size_t> changes(const image::Raster& a, const image::Raster& b) {
    std::set<std::size_t> out;
    for (std::size_t i = 0; i < a.samples.size(); ++i)
        if (a.samples[i] != b.samples[i]) out.insert(i);
    return out;
}

}  // namespace

TEST_SUITE("stc") {

TEST_CASE("STC: ida y vuelta con varias tasas, incluido el reparto en segmentos") {
    test::Rng rng(7);
    for (const auto& [n, m] : {std::pair<std::size_t, std::size_t>{64, 8}, {1000, 100}, {1000, 333}, {4096, 40},
                               {3'000'000, 40'000}}) {
        const std::size_t elements = n;
        CAPTURE(elements);
        const auto lsb = random_bits(rng, n);
        const auto message = random_bits(rng, m);
        std::vector<float> costs(n);
        for (auto& c : costs) c = 1.0f + float(rng.next() % 100);
        const auto stego = stego::stc::embed(lsb, costs, message);
        REQUIRE(stego.has_value());
        REQUIRE(stego->size() == n);
        CHECK(stego::stc::extract(*stego, m) == message);
    }
}

TEST_CASE("STC: cambia muchas menos ranuras que escribir bit a bit") {
    test::Rng rng(8);
    const std::size_t n = 20000, m = 2000;  // tasa 1/10
    const auto lsb = random_bits(rng, n);
    const auto message = random_bits(rng, m);
    const std::vector<float> costs(n, 1.0f);
    const auto stego = stego::stc::embed(lsb, costs, message);
    REQUIRE(stego.has_value());
    std::size_t changed = 0;
    for (std::size_t i = 0; i < n; ++i) changed += lsb[i] != (*stego)[i];
    CHECK(changed < m / 4);  // sin codificar serian m / 2
}

TEST_CASE("STC: evita las ranuras caras y respeta las de coste infinito") {
    test::Rng rng(9);
    const std::size_t n = 12000, m = 600;
    const auto lsb = random_bits(rng, n);
    const auto message = random_bits(rng, m);
    std::vector<float> costs(n);
    std::vector<bool> wet(n);
    for (std::size_t i = 0; i < n; ++i) {
        wet[i] = rng.next() % 2 == 0;  // la mitad, intocable
        costs[i] = wet[i] ? std::numeric_limits<float>::infinity() : 1.0f;
    }
    const auto stego = stego::stc::embed(lsb, costs, message);
    REQUIRE(stego.has_value());
    for (std::size_t i = 0; i < n; ++i)
        if (wet[i]) REQUIRE(lsb[i] == (*stego)[i]);
    CHECK(stego::stc::extract(*stego, m) == message);

    // Sin ninguna ranura libre no hay solucion salvo que el mensaje ya coincida
    const std::vector<float> all_wet(n, std::numeric_limits<float>::infinity());
    auto impossible = message;
    impossible[0] ^= stego::stc::extract(lsb, m)[0] ^ 1u;
    CHECK_FALSE(stego::stc::embed(lsb, all_wet, impossible).has_value());
}

TEST_CASE("sequence_length limita el ancho sin pasarse de lo disponible") {
    CHECK(stego::stc::sequence_length(100000, 10) == 10 * stego::stc::kMaxWidth);
    CHECK(stego::stc::sequence_length(100, 10) == 100);
}

}  // TEST_SUITE stc

TEST_SUITE("formato") {

TEST_CASE("Adaptativo: invariantes en los tres dominios, sin k y con limite prudente") {
    const Bytes payload = random_bytes(120);
    SUBCASE("espacial") {
        image::Raster r = test::natural_raster(96, 72);
        stego::PixelCarrier c(r);
        stego::embed(c, "pw", payload, params(EmbedMode::Adaptive));
        CHECK(stego::extract(c, "pw", Layout::Random, test::fast_kdf()) == payload);
    }
    SUBCASE("DCT") {
        const Bytes file = jpeg::compress_pixels(test::natural_raster(320, 240), 90);
        jpeg::CoefImage coefs = jpeg::read_coefficients(file);
        stego::DctCarrier c(coefs);
        stego::embed(c, "pw", payload, params(EmbedMode::Adaptive));
        CHECK(stego::extract(c, "pw", Layout::Random, test::fast_kdf()) == payload);
    }
    SUBCASE("paleta") {
        gif::Image img = test::natural_gif(120, 100);
        stego::PaletteCarrier c(img);
        stego::embed(c, "pw", payload, params(EmbedMode::Adaptive));
        CHECK(stego::extract(c, "pw", Layout::Random, test::fast_kdf()) == payload);
    }
    image::Raster r = test::natural_raster(96, 72);
    stego::PixelCarrier c(r);
    const std::size_t cap = stego::adaptive_payload_bytes(c);
    CHECK(cap < stego::max_payload_bytes(c, 1) / 3);
    CHECK(test::error_of([&] { stego::embed(c, "pw", random_bytes(cap + 1), params(EmbedMode::Adaptive)); }) ==
          ErrorCode::Capacity);
    stego::EngineParams two_bits = params(EmbedMode::Adaptive);
    two_bits.bits_per_slot = 2;
    CHECK(test::error_of([&] { stego::embed(c, "pw", payload, two_bits); }) == ErrorCode::InvalidArgument);
}

TEST_CASE("Adaptativo es el modo por defecto y falla en vez de subir la carga") {
    EmbedOptions o;
    o.password = "pw";
    o.kdf = test::fast_kdf();
    o.compress = false;
    CHECK(o.mode == EmbedMode::Adaptive);
    const Bytes cover = image::png_encode(test::natural_raster(96, 72));
    CHECK(test::error_of([&] { embed(cover, Payload::file("x", random_bytes(4000)), o, "c.png"); }) ==
          ErrorCode::Capacity);
    o.mode = EmbedMode::Replace;
    const EmbedResult r = embed(cover, Payload::file("x", random_bytes(4000)), o, "c.png");
    CHECK(r.notes.size() >= 1);  // se avisa de que este modo es mas detectable
}

TEST_CASE("Adaptativo: cambia menos que matching y concentra los cambios en la textura") {
    image::Raster base = test::natural_raster(200, 150);
    for (std::size_t px = 0; px < base.pixel_count(); ++px)  // mitad izquierda lisa, derecha ruidosa
        for (unsigned ch = 0; ch < 3; ++ch)
            base.at(px, ch) = px % 200 < 100 ? std::uint16_t(128) : std::uint16_t(64 + (px * 2654435761u >> 7) % 128);

    const Bytes payload = random_bytes(1200);
    image::Raster adaptive = base, match = base;
    stego::PixelCarrier ca(adaptive), cm(match);
    stego::embed(ca, "pw", payload, params(EmbedMode::Adaptive));
    stego::embed(cm, "pw", payload, params(EmbedMode::Match));

    const auto changed = changes(base, adaptive);
    CHECK(changed.size() < changes(base, match).size() / 2);
    std::size_t smooth = 0;
    for (const std::size_t i : changed) smooth += (i / 3) % 200 < 100;
    CHECK(smooth < changed.size() / 10);
}

TEST_CASE("v3: la sal y la cabecera tampoco delatan las zonas lisas") {
    image::Raster base(256, 256, 3, 8);
    test::Rng rng(11);
    for (std::size_t px = 0; px < base.pixel_count(); ++px)  // mitad izquierda lisa, derecha ruidosa
        for (unsigned ch = 0; ch < 3; ++ch)
            base.at(px, ch) = px % 256 < 128 ? std::uint16_t(200) : std::uint16_t(rng.next() % 256);

    for (const auto mode : {EmbedMode::Adaptive, EmbedMode::Match, EmbedMode::Replace}) {
        image::Raster stego_image = base;
        stego::PixelCarrier c(stego_image);
        stego::embed(c, "pw", random_bytes(32), params(mode));
        std::size_t smooth = 0;
        for (const std::size_t i : changes(base, stego_image)) smooth += (i / 3) % 256 < 128;
        if (mode == EmbedMode::Adaptive) CHECK(smooth <= 4);  // escribiendo bit a bit serian unos 90
        CHECK(stego::extract(c, "pw", Layout::Random, test::fast_kdf()).size() == 32);
    }
}

TEST_CASE("v3: la sal y la cabecera en JPEG cambian menos DC que escribiendolas bit a bit") {
    const Bytes file = jpeg::compress_pixels(test::natural_raster(640, 480), 90);
    jpeg::CoefImage coefs = jpeg::read_coefficients(file);
    const jpeg::CoefImage clean = coefs;
    stego::DctCarrier c(coefs);
    const Bytes payload = random_bytes(64);
    stego::embed(c, "pw", payload, params(EmbedMode::Adaptive));
    CHECK(stego::extract(c, "pw", Layout::Random, test::fast_kdf()) == payload);
    std::size_t dc_changes = 0;
    for (std::size_t comp = 0; comp < coefs.components.size(); ++comp)
        for (std::size_t i = 0; i < coefs.components[comp].coefs.size(); i += 64)
            dc_changes += coefs.components[comp].coefs[i] != clean.components[comp].coefs[i];
    CHECK(dc_changes < stego::kReservedSlots / 4);  // bit a bit cambiaria la mitad
}

TEST_CASE("v3: una imagen minima usa una ranura por bit y sigue funcionando") {
    image::Raster r = test::natural_raster(16, 16);  // W = 1
    stego::PixelCarrier c(r);
    const std::size_t cap = stego::max_payload_bytes(c, 1);
    REQUIRE(cap > 0);
    const Bytes payload = random_bytes(cap);
    stego::embed(c, "pw", payload, params(EmbedMode::Replace));
    CHECK(stego::extract(c, "pw", Layout::Random, test::fast_kdf()) == payload);
}

TEST_CASE("Se avisa si la imagen es casi toda lisa o si un PNG animado pierde fotogramas") {
    EmbedOptions o;
    o.password = "pw";
    o.kdf = test::fast_kdf();
    auto has_note = [](const EmbedResult& r, std::string_view text) {
        for (const auto& n : r.notes)
            if (n.find(text) != std::string::npos) return true;
        return false;
    };
    image::Raster flat(128, 128, 3, 8);
    std::fill(flat.samples.begin(), flat.samples.end(), std::uint16_t(200));
    CHECK(has_note(embed(image::png_encode(flat), Payload::text("hola"), o, "c.png"), "zonas lisas"));

    const image::Raster natural = test::natural_raster(128, 128);
    CHECK_FALSE(has_note(embed(image::png_encode(natural), Payload::text("hola"), o, "c.png"), "zonas lisas"));
    const Bytes apng = image::png_encode(natural, {image::PngChunk{"acTL", Bytes(8, 0)}});
    CHECK(has_note(embed(apng, Payload::text("hola"), o, "c.png"), "PNG es animado"));
}

TEST_CASE("Un nombre de fichero hostil no puede salir de la carpeta ni abrir dispositivos") {
    for (const auto& [hostile, safe] : std::vector<std::pair<std::string, std::string>>{
             {"C:evil.txt", "C_evil.txt"},
             {"nota.txt:oculto", "nota.txt_oculto"},
             {"..\\..\\x.exe", "x.exe"},
             {"../x.exe", "x.exe"},
             {"..", ""},
             {"NUL", "_NUL"},
             {"con.txt", "_con.txt"},
             {"COM1", "_COM1"},
             {"comun.txt", "comun.txt"},
             {"final. ", "final"},
             {"a\x1b[2Jb", "a_[2Jb"},
             {"datos ñ.bin", "datos ñ.bin"}}) {
        CAPTURE(hostile);
        CHECK(Payload::file(hostile, Bytes{1}).name == safe);

        // Tambien al leer: la imagen puede venir de otro programa que no lo filtre.
        Bytes packed{0x02, static_cast<std::uint8_t>(hostile.size())};
        append(packed, as_bytes(hostile));
        put_u32le(packed, 1);
        packed.push_back(0x7F);
        CHECK(stego::unpack_payload(packed).name == safe);
    }
}

TEST_CASE("v3: misma portadora y contrasena dan sal y posiciones distintas") {
    const image::Raster base = test::natural_raster(200, 150);
    const Bytes payload = random_bytes(500);
    image::Raster a = base, b = base;
    stego::PixelCarrier ca(a), cb(b);
    stego::embed(ca, "pw", payload, params(EmbedMode::Replace));
    stego::embed(cb, "pw", payload, params(EmbedMode::Replace));
    const auto ca_changes = changes(base, a), cb_changes = changes(base, b);
    std::size_t common = 0;
    for (const std::size_t i : ca_changes) common += cb_changes.count(i);
    // Ranuras independientes coinciden poco; con posiciones repetidas seria la mitad
    CHECK(common < ca_changes.size() / 4);
}

TEST_CASE("Matching no deja el peine de periodo 16 en el histograma") {
    // Antes, 15 y 16 (y todas las fronteras 16m) nunca se cruzaban y se vaciaban.
    image::Raster r(300, 300, 1, 8);
    test::Rng rng(3);
    for (auto& s : r.samples) s = static_cast<std::uint16_t>(rng.next() % 256);
    auto comb = [](const image::Raster& img) {
        std::array<std::uint64_t, 256> h{};
        for (const auto s : img.samples) ++h[s];
        double inner = 0, outer = 0;
        for (int m = 1; m < 16; ++m) {
            inner += double(h[16 * m - 1] + h[16 * m]);
            outer += double(h[16 * m - 2] + h[16 * m + 1]);
        }
        return inner / outer;
    };
    stego::PixelCarrier c(r);
    stego::embed(c, "pw", random_bytes(stego::max_payload_bytes(c, 1)), params(EmbedMode::Match));
    CHECK(comb(r) == doctest::Approx(1.0).epsilon(0.03));
}

TEST_CASE("Sustitucion en DCT: el histograma sigue simetrico y +-1 no se tocan") {
    const Bytes file = jpeg::compress_pixels(test::natural_raster(320, 240), 90);
    jpeg::CoefImage coefs = jpeg::read_coefficients(file);
    auto count = [&](int value) {
        std::uint64_t n = 0;
        for (const auto& comp : coefs.components)
            for (std::size_t i = 0; i < comp.coefs.size(); ++i) n += i % 64 != 0 && comp.coefs[i] == value;
        return n;
    };
    const auto plus_one = count(1), minus_one = count(-1);
    stego::DctCarrier c(coefs);
    stego::embed(c, "pw", random_bytes(stego::max_payload_bytes(c, 1)), params(EmbedMode::Replace));
    CHECK(count(1) == plus_one);
    CHECK(count(-1) == minus_one);
    CHECK(stego::extract(c, "pw", Layout::Random, test::fast_kdf()).size() > 0);
}

TEST_CASE("v3: un JPEG demasiado pequeno para la sal y la cabecera se rechaza") {
    const Bytes tiny = jpeg::compress_pixels(test::natural_raster(48, 48), 90);
    EmbedOptions o;
    o.password = "pw";
    o.kdf = test::fast_kdf();
    CHECK(test::error_of([&] { embed(tiny, Payload::text("x"), o, "c.jpg"); }) == ErrorCode::Capacity);
}

TEST_CASE("v3: alterar la sal o el cuerpo se detecta") {
    const Bytes payload = random_bytes(64);
    for (const auto mode : {EmbedMode::Adaptive, EmbedMode::Replace}) {
        image::Raster r = test::natural_raster(96, 72);
        stego::PixelCarrier c(r);
        stego::embed(c, "pw", payload, params(mode, Layout::Sequential));
        REQUIRE(stego::extract(c, "pw", Layout::Sequential, test::fast_kdf()) == payload);
        const image::Raster clean = r;
        std::size_t rejected = 0;
        for (std::size_t slot = 0; slot < r.samples.size(); slot += 97) {  // salt, cabecera o cuerpo
            r = clean;
            r.samples[slot] ^= 1u;
            const auto recovered = [&]() -> std::optional<Bytes> {
                try {
                    return stego::extract(c, "pw", Layout::Sequential, test::fast_kdf());
                } catch (const Error&) {
                    return std::nullopt;
                }
            }();
            // Un cambio fuera de las ranuras usadas no afecta; dentro, nunca da otro mensaje
            if (recovered) REQUIRE(*recovered == payload);
            else ++rejected;
        }
        CHECK(rejected > 0);
    }
}

TEST_CASE("Imagenes reales: v1, v2 y v3 se siguen leyendo") {
    for (const std::string version : {"v1", "v2", "v3"})
        for (const std::string ext : {"png", "jpg", "gif"}) {
            const std::string name = version + "_demo_mensaje." + ext;
            CAPTURE(name);
            const Bytes file = golden(name);
            ExtractOptions o;
            o.password = "icpu-demo";
            CHECK(extract(file, o, name).as_text() == kDemoMessage);
            o.password = "otra";
            CHECK(test::error_of([&] { extract(file, o, name); }) == ErrorCode::Authentication);
        }
    for (const std::string version : {"v1", "v2", "v3"}) {
        const std::string name = version + "_demo_secuencial.png";
        CAPTURE(name);
        ExtractOptions o;
        o.password = "icpu-demo";
        o.layout = Layout::Sequential;
        const Payload p = extract(golden(name), o, name);
        CHECK(p.kind == Payload::Kind::File);
        CHECK(p.name == "generar_muestras.py");
    }
}

TEST_CASE("Imagenes reales: una modificacion de una v2 se detecta") {
    image::Raster px = image::decode_raster(golden("v2_demo_mensaje.png"), image::ImageFormat::Png);
    for (auto& s : px.samples) s ^= 1u;  // la sal antigua no cambia, pero el AEAD lo detecta
    ExtractOptions o;
    o.password = "icpu-demo";
    CHECK(test::error_of([&] { extract(image::png_encode(px), o, "t.png"); }) == ErrorCode::Authentication);
}

}  // TEST_SUITE formato
