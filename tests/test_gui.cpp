// gui: el servidor HTTP local arrancado de verdad en un hilo, probado con un
// cliente HTTP. Cubre la seguridad (token, Host, cabeceras) y la API.

#include <doctest.h>

#include <httplib.h>

#include <thread>

#include "helpers.hpp"
#include "icpu/image/png.hpp"
#include "icpu/image/qoi.hpp"
#include "server.hpp"

using namespace icpu;

namespace {

struct TestServer {
    httplib::Server svr;
    std::string token = gui::new_session_token();
    int port = 0;
    std::thread thread;

    TestServer() {
        gui::configure(svr, token, port);
        port = svr.bind_to_any_port("127.0.0.1");
        REQUIRE(port > 0);
        thread = std::thread([this] { svr.listen_after_bind(); });
        svr.wait_until_ready();
    }
    ~TestServer() {
        svr.stop();
        thread.join();
    }
    httplib::Client client() const {
        httplib::Client c("127.0.0.1", port);
        c.set_read_timeout(120);
        return c;
    }
    httplib::Headers auth() const { return {{"X-ICPU-Token", token}}; }
};

// Extraccion minima de un campo de texto de una respuesta JSON plana (sin escapes complejos).
std::string json_string(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\":\"";
    const auto start = json.find(marker);
    if (start == std::string::npos) return {};
    const auto from = start + marker.size();
    return json.substr(from, json.find('"', from) - from);
}

std::string base64_decode(const std::string& in) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        return c == '+' ? 62 : c == '/' ? 63 : -1;
    };
    std::string out;
    int buf = 0, bits = 0;
    for (char c : in) {
        const int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

std::string png_cover() {
    const Bytes png = image::png_encode(test::natural_raster(96, 72));
    return {png.begin(), png.end()};
}

}  // namespace

TEST_SUITE("gui") {

TEST_CASE("Seguridad: token, Host y cabeceras del frontend") {
    TestServer s;
    auto c = s.client();

    auto r = c.Get("/api/version");  // sin token
    REQUIRE(r);
    CHECK(r->status == 403);
    r = c.Get("/api/version", httplib::Headers{{"X-ICPU-Token", "token-falso"}});
    CHECK(r->status == 403);
    r = c.Get("/api/version", s.auth());
    CHECK(r->status == 200);
    CHECK(r->body.find("\"version\"") != std::string::npos);

    // DNS rebinding: un Host ajeno se rechaza incluso con token
    httplib::Headers evil = s.auth();
    evil.emplace("Host", "evil.example:" + std::to_string(s.port));
    r = c.Get("/api/version", evil);
    CHECK(r->status == 403);

    // El frontend no necesita token, pero se sirve con CSP estricta
    r = c.Get("/");
    REQUIRE(r);
    CHECK(r->status == 200);
    CHECK(r->get_header_value("Content-Type").find("text/html") == 0);
    CHECK(r->get_header_value("Content-Security-Policy").find("default-src 'self'") != std::string::npos);
    CHECK(c.Get("/app.js")->status == 200);
    CHECK(c.Get("/no-existe.txt")->status == 404);
}

TEST_CASE("API: ocultar y extraer de punta a punta") {
    TestServer s;
    auto c = s.client();
    const std::string cover = png_cover();

    httplib::UploadFormDataItems embed_form{
        {"cover", cover, "portada.png", "image/png"},
        {"message", "Hola desde la prueba de la GUI ñ", "", ""},
        {"password", "clave-gui", "", ""},
    };
    auto r = c.Post("/api/embed", s.auth(), embed_form);
    REQUIRE(r);
    REQUIRE(r->status == 200);
    CHECK(json_string(r->body, "filename") == "portada.png");  // sin rastro de la herramienta
    CHECK(r->body.find("\"verified\":true") != std::string::npos);
    CHECK(r->body.find("\"psnr\":") != std::string::npos);
    CHECK_FALSE(json_string(r->body, "difference_map").empty());
    CHECK(r->body.find("\"preview\"") == std::string::npos);  // PNG: el navegador lo muestra solo
    const std::string stego = base64_decode(json_string(r->body, "image"));
    REQUIRE(stego.size() > 8);

    httplib::UploadFormDataItems extract_form{{"image", stego, "portada.png", "image/png"},
                                              {"password", "clave-gui", "", ""}};
    r = c.Post("/api/extract", s.auth(), extract_form);
    REQUIRE(r);
    CHECK(r->status == 200);
    CHECK(json_string(r->body, "text") == "Hola desde la prueba de la GUI ñ");

    extract_form[1].content = "otra-clave";
    r = c.Post("/api/extract", s.auth(), extract_form);
    CHECK(r->status == 422);
    CHECK(json_string(r->body, "code") == "autenticacion");

    // Errores de parametros: 400 con mensaje
    r = c.Post("/api/embed", s.auth(), httplib::UploadFormDataItems{{"message", "x", "", ""}});
    CHECK(r->status == 400);
    CHECK_FALSE(json_string(r->body, "error").empty());
}

TEST_CASE("API: vista previa en PNG de formatos que el navegador no muestra") {
    TestServer s;
    auto c = s.client();
    const Bytes qoi = image::qoi_encode(test::natural_raster(20, 10));
    auto r = c.Post("/api/preview", s.auth(),
                    httplib::UploadFormDataItems{{"image", std::string(qoi.begin(), qoi.end()), "x.qoi", ""}});
    REQUIRE(r);
    CHECK(r->status == 200);
    CHECK(r->get_header_value("Content-Type") == "image/png");
    CHECK(r->body.compare(1, 3, "PNG") == 0);
}

TEST_CASE("API: el experimento transmite el progreso y termina con el resultado") {
    TestServer s;
    auto c = s.client();
    httplib::UploadFormDataItems form{{"cover", png_cover(), "c.png", "image/png"},
                                      {"bits", "1", "", ""},
                                      {"modes", "replace", "", ""},
                                      {"layouts", "random", "", ""},
                                      {"fills", "0.1,0.5", "", ""}};
    auto r = c.Post("/api/bench", s.auth(), form);
    REQUIRE(r);
    CHECK(r->status == 200);
    CHECK(r->get_header_value("Content-Type").find("application/x-ndjson") == 0);
    std::vector<std::string> lines;
    for (std::size_t pos = 0, nl; (nl = r->body.find('\n', pos)) != std::string::npos; pos = nl + 1)
        lines.push_back(r->body.substr(pos, nl - pos));
    REQUIRE(lines.size() == 4);  // progreso 0/2, 1/2, 2/2 y resultado
    CHECK(lines[0].find("\"type\":\"progress\"") != std::string::npos);
    CHECK(lines[2].find("\"done\":2") != std::string::npos);
    CHECK(lines[3].find("\"type\":\"result\"") != std::string::npos);
    CHECK(lines[3].find("\"runs\":[") != std::string::npos);

    // Un error de parametros dentro del experimento llega como linea "error"
    form[1].content = "9";
    r = c.Post("/api/bench", s.auth(), form);
    REQUIRE(r);
    CHECK(r->body.find("\"type\":\"error\"") != std::string::npos);
}

}  // TEST_SUITE gui
