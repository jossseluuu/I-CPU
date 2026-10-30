// core: bits, bytes, JSON, ficheros y el analizador de argumentos de la CLI.

#include <doctest.h>

#include <filesystem>
#include <limits>

#include "args.hpp"
#include "helpers.hpp"
#include "icpu/core/bits.hpp"
#include "icpu/core/file.hpp"
#include "icpu/core/json.hpp"

using namespace icpu;

TEST_SUITE("core") {

TEST_CASE("BitWriter/BitReader: MSB primero e ida y vuelta") {
    BitWriter w;
    w.write(0b101, 3);
    w.write(0x1F, 5);
    w.write(0xABCD, 16);
    w.write(1, 1);
    CHECK(w.bit_count() == 25);
    const Bytes b = w.take();
    REQUIRE(b.size() == 4);
    CHECK(b[0] == 0xBF);  // 101 11111
    CHECK(b[1] == 0xAB);
    CHECK(b[2] == 0xCD);
    CHECK(b[3] == 0x80);  // 1 + relleno con ceros

    BitReader r(b);
    CHECK(r.read(3) == 0b101);
    CHECK(r.read(5) == 0x1F);
    CHECK(r.read(16) == 0xABCD);
    CHECK(r.read(1) == 1);
    CHECK(r.remaining() == 7);
}

TEST_CASE("Serializacion little/big endian") {
    Bytes b;
    put_u16le(b, 0x1234);
    put_u16be(b, 0x1234);
    put_u32le(b, 0xDEADBEEF);
    put_u32be(b, 0xDEADBEEF);
    CHECK(get_u16le(b.data()) == 0x1234);
    CHECK(get_u16be(b.data() + 2) == 0x1234);
    CHECK(get_u32le(b.data() + 4) == 0xDEADBEEF);
    CHECK(get_u32be(b.data() + 8) == 0xDEADBEEF);
    CHECK(b[0] == 0x34);
    CHECK(b[2] == 0x12);
    CHECK(to_hex(Bytes{0x00, 0xAB, 0xFF}) == "00abff");
}

TEST_CASE("JSON: estructura, comas y escapado") {
    json::Writer w;
    w.begin_object()
        .field("a", 1)
        .key("b")
        .begin_array()
        .value(true)
        .value("x\"y\\z\n")
        .null()
        .end_array()
        .field("c", std::string("\x01ñ"))
        .key("d")
        .begin_object()
        .end_object()
        .end_object();
    CHECK(w.str() == "{\"a\":1,\"b\":[true,\"x\\\"y\\\\z\\n\",null],\"c\":\"\\u0001ñ\",\"d\":{}}");

    json::Writer n;
    n.begin_array().value(std::numeric_limits<double>::infinity()).end_array();
    CHECK(n.str() == "[null]");
}

TEST_CASE("Ficheros: escritura atomica, lectura y rutas UTF-8") {
    const auto dir = std::filesystem::temp_directory_path() / "icpu_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / utf8_path("prueba_ñandú.BIN");
    const Bytes data{1, 2, 3, 0, 255};
    write_file(path, data);
    CHECK(read_file(path) == data);
    CHECK(lower_extension(path) == "bin");
    CHECK(path_utf8(path.filename()) == "prueba_ñandú.BIN");
    std::filesystem::remove(path);
    CHECK(test::error_of([&] { read_file(path); }) == ErrorCode::Io);
}

}  // TEST_SUITE core

TEST_SUITE("cli") {

TEST_CASE("Args: posicionales, valores, banderas y opciones desconocidas") {
    using icpu::cli::Args;
    const Args a({"in.png", "-o", "out.png", "--bits=2", "--json", "extra"}, {"--json"});
    CHECK(a.positionals() == std::vector<std::string>{"in.png", "extra"});
    CHECK(a.value({"-o", "--output"}) == "out.png");
    CHECK(a.value({"--bits"}) == "2");
    CHECK(a.value_or({"--mode"}, "replace") == "replace");
    CHECK(a.flag({"--json"}));
    CHECK_FALSE(a.flag({"--force"}));
    CHECK_NOTHROW(a.reject_unknown());

    const Args typo({"x", "--outptu", "y"}, {});
    CHECK(test::error_of([&] { typo.reject_unknown(); }) == ErrorCode::InvalidArgument);

    // Opcion al final sin valor: si se consulta, "necesita un valor"; si no, "desconocida".
    const Args missing({"x", "--bits"}, {});
    CHECK(test::error_of([&] { (void)missing.value({"--bits"}); }) == ErrorCode::InvalidArgument);
    const Args unknown({"x", "--jsn"}, {"--json"});
    CHECK(unknown.flag({"--json"}) == false);
    try {
        unknown.reject_unknown();
        FAIL("deberia rechazar --jsn");
    } catch (const Error& e) {
        CHECK(std::string(e.what()).find("desconocida") != std::string::npos);
    }

    // "--" y "-" son posicionales (p. ej. stdin)
    const Args dash({"-", "--"}, {});
    CHECK(dash.positionals().size() == 2);
}

}  // TEST_SUITE cli
