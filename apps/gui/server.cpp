#include "server.hpp"

#include <httplib.h>

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

#include "assets.hpp"
#include "icpu/analysis/metrics.hpp"
#include "icpu/analysis/visual.hpp"
#include "icpu/core/file.hpp"
#include "icpu/core/json.hpp"
#include "icpu/crypto/random.hpp"
#include "icpu/image/codec.hpp"
#include "icpu/report.hpp"
#include "icpu/icpu.hpp"

#if defined(_WIN32)
#  include <windows.h>
#  include <shellapi.h>
#endif

namespace icpu::gui {

namespace {

constexpr std::size_t kMaxUpload = 256u * 1024 * 1024;

// ---- Utilidades ----

std::string base64(ByteSpan data) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        const std::uint32_t v = (std::uint32_t(data[i]) << 16) | (std::uint32_t(data[i + 1]) << 8) | data[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    if (const std::size_t rest = data.size() - i; rest > 0) {
        std::uint32_t v = std::uint32_t(data[i]) << 16;
        if (rest == 2) v |= std::uint32_t(data[i + 1]) << 8;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += rest == 2 ? kAlphabet[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string random_token() {
    std::uint8_t raw[16];
    crypto::os_random(raw, sizeof raw);
    return to_hex(ByteSpan(raw, sizeof raw));
}

bool constant_time_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

const char* mime_for(image::ImageFormat f) {
    switch (f) {
        case image::ImageFormat::Png: return "image/png";
        case image::ImageFormat::Jpeg: return "image/jpeg";
        case image::ImageFormat::Gif: return "image/gif";
        case image::ImageFormat::Bmp: return "image/bmp";
        default: return "application/octet-stream";
    }
}

bool browser_can_show(image::ImageFormat f) {
    return f == image::ImageFormat::Png || f == image::ImageFormat::Jpeg || f == image::ImageFormat::Gif ||
           f == image::ImageFormat::Bmp;
}

std::string png_base64(const image::Raster& r) { return base64(image::encode_raster(r, image::ImageFormat::Png)); }

int http_status(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidArgument:
        case ErrorCode::UnsupportedFormat:
        case ErrorCode::CorruptData:
        case ErrorCode::Capacity: return 400;
        case ErrorCode::Authentication: return 422;
        case ErrorCode::Io:
        case ErrorCode::Internal: return 500;
    }
    return 500;
}

void send_json(httplib::Response& res, const json::Writer& w, int status = 200) {
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(w.str(), "application/json; charset=utf-8");
}

void send_error(httplib::Response& res, int status, const std::string& code, const std::string& message) {
    json::Writer w;
    w.begin_object().field("error", message).field("code", code).end_object();
    send_json(res, w, status);
}

// Envuelve un manejador: traduce las excepciones a respuestas JSON de error.
template <typename F>
httplib::Server::Handler guarded(F handler) {
    return [handler](const httplib::Request& req, httplib::Response& res) {
        try {
            handler(req, res);
        } catch (const Error& e) {
            send_error(res, http_status(e.code()), error_code_name(e.code()), e.what());
        } catch (const std::exception& e) {
            send_error(res, 500, "interno", std::string("Error inesperado: ") + e.what());
        }
    };
}

// ---- Lectura de formularios multipart ----

const httplib::FormData& require_file(const httplib::Request& req, const char* key, const char* what) {
    if (!req.form.has_file(key)) fail(ErrorCode::InvalidArgument, std::string("Falta ") + what);
    // get_file devuelve por valor; se busca la referencia para no copiar imagenes grandes.
    for (const auto& [name, file] : req.form.files)
        if (name == key) return file;
    fail(ErrorCode::InvalidArgument, std::string("Falta ") + what);
}

std::string field_or(const httplib::Request& req, const char* key, std::string fallback) {
    return req.form.has_field(key) ? req.form.get_field(key) : std::move(fallback);
}

int int_field(const httplib::Request& req, const char* key, int fallback, int lo, int hi) {
    if (!req.form.has_field(key)) return fallback;
    const std::string s = req.form.get_field(key);
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end != '\0' || v < lo || v > hi)
        fail(ErrorCode::InvalidArgument, std::string("Valor invalido para ") + key + ": " + s);
    return static_cast<int>(v);
}

std::vector<std::string> list_field(const httplib::Request& req, const char* key, const std::string& fallback) {
    std::vector<std::string> out;
    std::stringstream ss(field_or(req, key, fallback));
    for (std::string item; std::getline(ss, item, ',');)
        if (!item.empty()) out.push_back(item);
    if (out.empty()) fail(ErrorCode::InvalidArgument, std::string("Lista vacia: ") + key);
    return out;
}

EmbedMode parse_mode(const std::string& s) {
    if (s == "replace") return EmbedMode::Replace;
    if (s == "match") return EmbedMode::Match;
    if (s == "adaptive") return EmbedMode::Adaptive;
    fail(ErrorCode::InvalidArgument, "Modo desconocido: " + s);
}

Layout parse_layout(const std::string& s) {
    if (s == "random") return Layout::Random;
    if (s == "sequential") return Layout::Sequential;
    fail(ErrorCode::InvalidArgument, "Orden desconocido: " + s);
}

std::optional<image::ImageFormat> parse_output(const std::string& s) {
    if (s.empty() || s == "auto") return std::nullopt;
    const image::ImageFormat f = image::format_from_extension(s);
    if (f == image::ImageFormat::Unknown || !image::can_write(f))
        fail(ErrorCode::InvalidArgument, "Formato de salida no soportado: " + s);
    return f;
}

std::string stem_of(const std::string& filename) {
    const std::string stem = path_utf8(utf8_path(filename).stem());
    return stem.empty() ? "imagen" : stem;
}

// ---- Manejadores de la API ----

void handle_info(const httplib::Request& req, httplib::Response& res) {
    const auto& file = require_file(req, "image", "la imagen");
    const ImageInfo info = inspect(as_bytes(file.content), file.filename);
    json::Writer w;
    w.begin_object()
        .field("format", image::format_name(info.format))
        .field("width", info.width)
        .field("height", info.height)
        .field("channels", info.channels)
        .field("depth", info.depth)
        .field("frames", info.frames)
        .field("file_bytes", std::uint64_t(info.file_bytes))
        .field("jpeg_quality", info.jpeg_quality)
        .field("jpeg_progressive", info.jpeg_progressive)
        .field("jpeg_subsampling", info.jpeg_subsampling)
        .key("capacities")
        .begin_array();
    for (const auto& c : info.capacities)
        w.begin_object()
            .field("output", image::format_name(c.output))
            .field("extension", image::default_extension(c.output))
            .field("domain", image::domain_name(c.domain))
            .field("bits_per_slot", c.bits_per_slot)
            .field("usable_slots", c.usable_slots)
            .field("message_bytes", std::uint64_t(c.message_bytes))
            .field("adaptive_bytes", std::uint64_t(c.adaptive_bytes))
            .end_object();
    w.end_array().end_object();
    send_json(res, w);
}

void handle_embed(const httplib::Request& req, httplib::Response& res) {
    const auto& cover = require_file(req, "cover", "la imagen portadora");
    EmbedOptions opt;
    opt.password = field_or(req, "password", "");
    const std::string bits = field_or(req, "bits", "auto");
    opt.bits_per_slot = bits == "auto" ? 0u : static_cast<unsigned>(int_field(req, "bits", 0, 1, 4));
    opt.mode = parse_mode(field_or(req, "mode", "adaptive"));
    opt.layout = parse_layout(field_or(req, "layout", "random"));
    opt.output_format = parse_output(field_or(req, "format", "auto"));
    opt.jpeg_quality = int_field(req, "quality", 90, 1, 100);
    opt.compress = field_or(req, "compress", "1") != "0";

    Payload payload;
    if (req.form.has_file("payload")) {
        const auto& f = require_file(req, "payload", "el fichero a ocultar");
        const std::string name = path_utf8(utf8_path(f.filename).filename());
        payload = Payload::file(name.empty() ? "fichero.bin" : name, Bytes(f.content.begin(), f.content.end()));
    } else {
        payload = Payload::text(field_or(req, "message", ""));
    }

    const EmbedResult r = embed(as_bytes(cover.content), payload, opt, cover.filename);
    const double fill = r.capacity_bytes ? double(r.packed_bytes) / double(r.capacity_bytes) : 0;

    // Para el comparador: pixeles de ambas imagenes, distorsion y mapa de diferencias.
    const image::Raster cover_px = load_pixels(as_bytes(cover.content), cover.filename);
    const image::Raster stego_px = load_pixels(r.image, std::string("x.") + image::default_extension(r.format));
    const analysis::QualityMetrics q = analysis::compare(cover_px, stego_px);

    json::Writer w;
    w.begin_object()
        .field("filename", stem_of(cover.filename) + "." + image::default_extension(r.format))
        .field("mime", mime_for(r.format))
        .field("format", image::format_name(r.format))
        .field("domain", image::domain_name(r.domain))
        .field("bits_per_slot", r.bits_per_slot)
        .field("message_bytes", std::uint64_t(r.message_bytes))
        .field("packed_bytes", std::uint64_t(r.packed_bytes))
        .field("compressed", r.compressed)
        .field("capacity_bytes", std::uint64_t(r.capacity_bytes))
        .field("fill_ratio", fill)
        .field("slots_used", r.stats.slots_used)
        .field("slots_changed", r.stats.slots_changed)
        .field("usable_slots", r.usable_slots)
        .field("verified", r.verified)
        .field("cover_bytes", std::uint64_t(cover.content.size()))
        .field("image_bytes", std::uint64_t(r.image.size()))
        .key("notes")
        .begin_array();
    for (const auto& n : r.notes) w.value(n);
    w.end_array()
        .key("quality")
        .begin_object()
        .field("psnr", q.psnr)
        .field("ssim", q.ssim)
        .field("changed_ratio", q.changed_ratio())
        .field("max_abs_diff", q.max_abs_diff)
        .end_object()
        .field("difference_map", png_base64(analysis::difference_map(cover_px, stego_px)));
    // Vistas previas en PNG solo si el navegador no sabe mostrar el formato original
    const auto cover_format = image::detect_format(as_bytes(cover.content), lower_extension(utf8_path(cover.filename)));
    if (!browser_can_show(cover_format)) w.field("cover_preview", png_base64(cover_px));
    if (!browser_can_show(r.format)) w.field("preview", png_base64(stego_px));
    w.field("image", base64(r.image)).end_object();
    send_json(res, w);
}

// Imagen convertida a PNG (para mostrar QOI, TGA, PNM, PSD... que el navegador no entiende).
void handle_preview(const httplib::Request& req, httplib::Response& res) {
    const auto& file = require_file(req, "image", "la imagen");
    const Bytes png = image::encode_raster(load_pixels(as_bytes(file.content), file.filename), image::ImageFormat::Png);
    res.set_header("Cache-Control", "no-store");
    res.set_content(reinterpret_cast<const char*>(png.data()), png.size(), "image/png");
}

void handle_extract(const httplib::Request& req, httplib::Response& res) {
    const auto& file = require_file(req, "image", "la imagen");
    ExtractOptions opt;
    opt.password = field_or(req, "password", "");
    opt.layout = parse_layout(field_or(req, "layout", "random"));
    const Payload p = extract(as_bytes(file.content), opt, file.filename);
    json::Writer w;
    w.begin_object()
        .field("kind", p.kind == Payload::Kind::File ? "file" : "text")
        .field("name", p.name)
        .field("bytes", std::uint64_t(p.data.size()));
    if (p.kind == Payload::Kind::Text) w.field("text", p.as_text());
    w.field("data", base64(p.data)).end_object();
    send_json(res, w);
}

void write_chi(json::Writer& w, const char* key, const analysis::ChiSquareResult& c) {
    w.key(key)
        .begin_object()
        .field("statistic", c.statistic)
        .field("dof", c.degrees_of_freedom)
        .field("p", c.p_value)
        .field("verdict", chi_square_verdict(c.p_value))
        .end_object();
}

void handle_analyze(const httplib::Request& req, httplib::Response& res) {
    const auto& file = require_file(req, "image", "la imagen a analizar");
    const unsigned steps = static_cast<unsigned>(int_field(req, "steps", 20, 1, 100));
    const AnalysisReport report = analyze(as_bytes(file.content), file.filename, steps);

    json::Writer w;
    w.begin_object()
        .field("format", image::format_name(report.format))
        .field("width", report.pixels.width)
        .field("height", report.pixels.height);
    if (report.chi_square) write_chi(w, "chi_square", *report.chi_square);
    if (report.chi_square_dct) write_chi(w, "chi_square_dct", *report.chi_square_dct);
    if (report.chi_square_sweep) {
        w.key("chi_square_sweep").begin_array();
        const auto& s = *report.chi_square_sweep;
        for (std::size_t i = 0; i < s.fraction.size(); ++i)
            w.begin_object()
                .field("fraction", s.fraction[i])
                .field("p", s.p_value[i])
                .field("window_p", s.window_p[i])
                .end_object();
        w.end_array();
    }
    if (report.rs)
        w.key("rs")
            .begin_object()
            .field("estimated_rate", report.rs->estimated_rate)
            .field("rm", report.rs->rm)
            .field("sm", report.rs->sm)
            .field("r_m", report.rs->r_m)
            .field("s_m", report.rs->s_m)
            .field("verdict", rs_verdict(report.rs->estimated_rate))
            .end_object();

    w.key("histograms").begin_array();
    for (const auto& channel : analysis::histograms(report.pixels)) {
        w.begin_array();
        for (const auto v : channel) w.value(std::uint64_t(v));
        w.end_array();
    }
    w.end_array();

    auto png_b64 = [](const image::Raster& r) { return base64(image::encode_raster(r, image::ImageFormat::Png)); };
    w.field("bit_plane_0", png_b64(analysis::bit_plane(report.pixels, 0)))
        .field("bit_plane_1", png_b64(analysis::bit_plane(report.pixels, 1)));

    if (req.form.has_file("cover")) {
        const auto& cover_file = require_file(req, "cover", "la portadora");
        const image::Raster cover = load_pixels(as_bytes(cover_file.content), cover_file.filename);
        require(cover.width == report.pixels.width && cover.height == report.pixels.height,
                ErrorCode::InvalidArgument, "La portadora y la imagen deben tener las mismas dimensiones");
        const analysis::QualityMetrics q = analysis::compare(cover, report.pixels);
        w.key("quality")
            .begin_object()
            .field("mse", q.mse)
            .field("psnr", q.psnr)
            .field("ssim", q.ssim)
            .field("changed_ratio", q.changed_ratio())
            .field("max_abs_diff", q.max_abs_diff)
            .end_object()
            .field("difference_map", png_b64(analysis::difference_map(cover, report.pixels)));
    }
    w.end_object();
    send_json(res, w);
}

std::string bench_json(const BenchReport& report);

void handle_bench(const httplib::Request& req, httplib::Response& res) {
    const auto& cover = require_file(req, "cover", "la imagen portadora");
    BenchOptions opt;
    opt.fills.clear();
    opt.bits.clear();
    opt.modes.clear();
    opt.layouts.clear();
    for (const auto& s : list_field(req, "fills", "0.05,0.1,0.25,0.5,1")) {
        char* end = nullptr;
        const double v = std::strtod(s.c_str(), &end);
        if (*end != '\0') fail(ErrorCode::InvalidArgument, "Llenado invalido: " + s);
        opt.fills.push_back(v);
    }
    for (const auto& s : list_field(req, "bits", "1,2")) opt.bits.push_back(static_cast<unsigned>(std::atoi(s.c_str())));
    for (const auto& s : list_field(req, "modes", "adaptive,replace,match")) opt.modes.push_back(parse_mode(s));
    for (const auto& s : list_field(req, "layouts", "random,sequential")) opt.layouts.push_back(parse_layout(s));
    opt.output_format = parse_output(field_or(req, "format", "auto"));

    // Respuesta en streaming, una linea JSON por evento (NDJSON):
    //   {"type":"progress","done":3,"total":20} ... {"type":"result",...} o {"type":"error",...}
    // Los datos se copian: el proveedor se ejecuta despues de que vuelva este manejador.
    auto data = std::make_shared<std::string>(cover.content);
    res.set_header("Cache-Control", "no-store");
    res.set_chunked_content_provider(
        "application/x-ndjson; charset=utf-8",
        [data, name = cover.filename, opt](std::size_t, httplib::DataSink& sink) {
            auto write_line = [&sink](const std::string& s) {
                const std::string line = s + "\n";
                // Si el navegador se ha ido (pestana cerrada), se aborta el experimento.
                if (!sink.write(line.data(), line.size())) throw std::runtime_error("cliente desconectado");
            };
            try {
                const BenchReport report = bench(as_bytes(*data), opt, name, [&](std::size_t done, std::size_t total) {
                    json::Writer p;
                    p.begin_object()
                        .field("type", "progress")
                        .field("done", std::uint64_t(done))
                        .field("total", std::uint64_t(total))
                        .end_object();
                    write_line(p.str());
                });
                write_line(bench_json(report));
            } catch (const Error& e) {
                json::Writer w;
                w.begin_object().field("type", "error").field("error", e.what()).end_object();
                write_line(w.str());
            } catch (const std::exception&) {
                return false;
            }
            sink.done();
            return true;
        });
}

std::string bench_json(const BenchReport& report) {
    auto write_detection = [](json::Writer& w, const Detection& d) {
        w.field("chi2_p", d.chi_p).field("rs_rate", d.rs_rate).field("dct_chi2_p", d.dct_p);
    };
    json::Writer w;
    w.begin_object()
        .field("type", "result")
        .field("output", image::format_name(report.output))
        .field("domain", image::domain_name(report.domain));
    if (report.baseline) {
        w.key("baseline").begin_object();
        write_detection(w, *report.baseline);
        w.end_object();
    }
    w.key("runs").begin_array();
    for (const auto& r : report.runs) {
        w.begin_object()
            .field("bits", r.bits)
            .field("mode", mode_name(r.mode))
            .field("layout", layout_name(r.layout))
            .field("fill", r.fill)
            .field("message_bytes", std::uint64_t(r.message_bytes));
        if (!r.error.empty()) {
            w.field("error", r.error).end_object();
            continue;
        }
        w.field("changed_ratio", r.changed_ratio).field("psnr", r.psnr).field("ssim", r.ssim);
        write_detection(w, r.detection);
        w.field("seconds", r.seconds).end_object();
    }
    w.end_array().end_object();
    return w.str();
}

void open_in_browser(const std::string& url) {
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::system(("open '" + url + "' >/dev/null 2>&1 &").c_str());
#else
    std::system(("xdg-open '" + url + "' >/dev/null 2>&1 &").c_str());
#endif
}

}  // namespace

std::string new_session_token() { return random_token(); }

// ---- TabTracker ----

void TabTracker::seen(const std::string& id, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    if (tabs_.size() >= kMaxTabs && !tabs_.contains(id)) return;  // limite defensivo
    tabs_[id] = now;
    ever_ = true;
}

void TabTracker::bye(const std::string& id, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    if (tabs_.erase(id) > 0 && tabs_.empty()) empty_since_ = now;
}

void TabTracker::prune(Clock::time_point now, Clock::duration stale) {
    const bool had = !tabs_.empty();
    std::erase_if(tabs_, [&](const auto& kv) { return now - kv.second > stale; });
    if (had && tabs_.empty()) empty_since_ = now;
}

bool TabTracker::abandoned(Clock::time_point now, Clock::duration grace, Clock::duration stale) {
    std::lock_guard lock(mutex_);
    prune(now, stale);
    return ever_ && tabs_.empty() && now - empty_since_ >= grace;
}

std::size_t TabTracker::open_tabs(Clock::time_point now, Clock::duration stale) {
    std::lock_guard lock(mutex_);
    prune(now, stale);
    return tabs_.size();
}

void configure(httplib::Server& svr, const std::string& token, const int& port, Lifecycle* life) {
    svr.set_payload_max_length(kMaxUpload);

    // Comprobaciones de seguridad antes de cualquier ruta (ver server.hpp).
    svr.set_pre_routing_handler([&token, &port](const httplib::Request& req, httplib::Response& res) {
        const std::string host = req.get_header_value("Host");
        const std::string p = std::to_string(port);
        if (host != "127.0.0.1:" + p && host != "localhost:" + p) {
            send_error(res, 403, "host", "Host no permitido");
            return httplib::Server::HandlerResponse::Handled;
        }
        if (req.path.rfind("/api/", 0) == 0 && !constant_time_equal(req.get_header_value("X-ICPU-Token"), token)) {
            send_error(res, 403, "token", "Token de sesion invalido: abre la GUI desde el enlace que muestra la consola");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    svr.Post("/api/info", guarded(handle_info));
    svr.Post("/api/embed", guarded(handle_embed));
    svr.Post("/api/extract", guarded(handle_extract));
    svr.Post("/api/analyze", guarded(handle_analyze));
    svr.Post("/api/bench", guarded(handle_bench));
    svr.Post("/api/preview", guarded(handle_preview));
    svr.Post("/api/shutdown", [&svr, life](const httplib::Request&, httplib::Response& res) {
        json::Writer w;
        w.begin_object().field("ok", true).end_object();
        send_json(res, w);
        if (life && life->request_shutdown)
            life->request_shutdown();  // run_server cierra el proceso tras enviar la respuesta
        else
            std::thread([&svr] { svr.stop(); }).detach();
    });
    // Presencia de las pestanas: event = hello | ping | bye, tab = identificador aleatorio de la pestana.
    svr.Post("/api/presence", guarded([life](const httplib::Request& req, httplib::Response& res) {
        const std::string tab = field_or(req, "tab", "");
        const std::string event = field_or(req, "event", "");
        if (tab.empty() || tab.size() > 64) fail(ErrorCode::InvalidArgument, "Identificador de pestana invalido");
        if (event != "hello" && event != "ping" && event != "bye")
            fail(ErrorCode::InvalidArgument, "Evento de presencia invalido: " + event);
        if (life) {
            const auto now = TabTracker::Clock::now();
            if (event == "bye")
                life->tabs.bye(tab, now);
            else
                life->tabs.seen(tab, now);
        }
        json::Writer w;
        w.begin_object().field("ok", true).end_object();
        send_json(res, w);
    }));
    svr.Get("/api/version", [](const httplib::Request&, httplib::Response& res) {
        json::Writer w;
        w.begin_object().field("version", version()).end_object();
        send_json(res, w);
    });

    // Frontend embebido
    svr.Get(R"(/.*)", [](const httplib::Request& req, httplib::Response& res) {
        const Asset* a = find_asset(req.path == "/" ? "/index.html" : req.path);
        if (!a) {
            res.status = 404;
            res.set_content("No encontrado", "text/plain; charset=utf-8");
            return;
        }
        res.set_header("Content-Security-Policy",
                       "default-src 'self'; img-src 'self' data: blob:; style-src 'self'; script-src 'self'; "
                       "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'");
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("Referrer-Policy", "no-referrer");
        res.set_header("Cache-Control", "no-cache");
        res.set_content(reinterpret_cast<const char*>(a->data), a->size, a->mime);
    });
}

namespace {

// Ctrl+C, Ctrl+Break o cerrar la consola. El manejador solo marca la peticion;
// el hilo de ciclo de vida la atiende (no se puede hacer casi nada dentro de un
// manejador de senal).
std::atomic<bool> g_console_stop{false};

#if defined(_WIN32)
BOOL WINAPI on_console_event(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            g_console_stop = true;
            return TRUE;
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            // Windows termina el proceso al volver de aqui: se da un momento al cierre ordenado.
            g_console_stop = true;
            Sleep(1500);
            return TRUE;
        default: return FALSE;
    }
}

// Instancia unica: un mutex con nombre detecta si ya hay una I-CPU abierta y un
// evento con nombre le pide que abra otra pestana. Asi no quedan servidores
// olvidados y el token nunca sale del proceso que lo creo.
constexpr const wchar_t* kInstanceMutex = L"Local\\ICPU-GUI-instance";
constexpr const wchar_t* kOpenEvent = L"Local\\ICPU-GUI-open-browser";
#else
extern "C" void on_signal(int) { g_console_stop = true; }
#endif

constexpr auto kTabGrace = std::chrono::seconds(10);   // margen para recargar la pagina
constexpr auto kTabStale = std::chrono::seconds(150);  // las pestanas en segundo plano avisan menos
constexpr auto kDrainTimeout = std::chrono::seconds(2);

}  // namespace

int run_server(const ServerConfig& config) {
#if defined(_WIN32)
    HANDLE instance = CreateMutexW(nullptr, FALSE, kInstanceMutex);
    if (instance && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, kOpenEvent)) {
            SetEvent(ev);
            CloseHandle(ev);
            std::cout << "I-CPU ya esta abierto: se ha abierto otra pestana en el navegador.\n";
            CloseHandle(instance);
            return 0;
        }
        // La otra instancia aun esta arrancando o cerrandose: se sigue con una nueva.
    }
    HANDLE open_event = CreateEventW(nullptr, FALSE, FALSE, kOpenEvent);
    SetConsoleCtrlHandler(on_console_event, TRUE);
#else
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
#endif

    httplib::Server svr;
    const std::string token = new_session_token();
    int port = 0;

    // Peticion de cierre: la hace el boton de la web, la consola o el vigilante de pestanas.
    std::mutex mutex;
    std::condition_variable cv;
    bool shutdown_requested = false;
    bool listen_done = false;
    std::string reason;
    auto request_shutdown = [&](const std::string& why) {
        {
            std::lock_guard lock(mutex);
            if (shutdown_requested) return;
            shutdown_requested = true;
            reason = why;
        }
        cv.notify_all();
    };

    Lifecycle life;
    life.request_shutdown = [&] { request_shutdown("boton de apagado de la web"); };
    configure(svr, token, port, &life);

    if (config.port > 0) {
        if (!svr.bind_to_port("127.0.0.1", config.port)) {
            std::cerr << "No se puede usar el puerto " << config.port << " (esta ocupado?)\n";
            return 1;
        }
        port = config.port;
    } else {
        port = svr.bind_to_any_port("127.0.0.1");
        if (port <= 0) {
            std::cerr << "No se pudo abrir ningun puerto local\n";
            return 1;
        }
    }

    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/#t=" + token;
    std::cout << "I-CPU " << version() << " - interfaz grafica\n\n"
              << "  Abre en el navegador: " << url << "\n\n"
              << "Para terminar: boton de apagado de la web, Ctrl+C aqui o cerrar todas sus pestanas.\n"
              << std::flush;
    if (config.open_browser) open_in_browser(url);

    // Hilo de ciclo de vida: espera una peticion de cierre (o la detecta), detiene el
    // servidor y, si alguna operacion no termina a tiempo, cierra el proceso igualmente.
    std::thread lifecycle([&] {
        std::unique_lock lock(mutex);
        while (!shutdown_requested) {
            cv.wait_for(lock, std::chrono::milliseconds(250));
            if (shutdown_requested) break;
            lock.unlock();
            std::string why;
            if (g_console_stop) why = "Ctrl+C o cierre de la consola";
            else if (life.tabs.abandoned(TabTracker::Clock::now(), kTabGrace, kTabStale))
                why = "se han cerrado todas las pestanas";
#if defined(_WIN32)
            if (open_event && WaitForSingleObject(open_event, 0) == WAIT_OBJECT_0) open_in_browser(url);
#endif
            lock.lock();
            if (!why.empty() && !shutdown_requested) {
                shutdown_requested = true;
                reason = why;
            }
        }
        lock.unlock();

        // Deja que la respuesta de /api/shutdown llegue al navegador.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::cout << "\nCerrando I-CPU (" << reason << ")...\n" << std::flush;
        svr.stop();

        lock.lock();
        if (!cv.wait_for(lock, kDrainTimeout, [&] { return listen_done; })) {
            // Una operacion larga (experimento, STC en una imagen grande) sigue en marcha.
            // Su resultado ya no tiene a quien llegar: se cancela terminando el proceso.
            std::cout << "Se cancelan las operaciones en curso.\nServidor cerrado.\n" << std::flush;
            std::_Exit(0);
        }
    });

    svr.listen_after_bind();
    {
        std::lock_guard lock(mutex);
        listen_done = true;
        if (!shutdown_requested) {  // el servidor se paro por otro motivo
            shutdown_requested = true;
            reason = "el servidor se ha detenido";
        }
    }
    cv.notify_all();
    lifecycle.join();
    std::cout << "Servidor cerrado.\n" << std::flush;
#if defined(_WIN32)
    if (open_event) CloseHandle(open_event);
    if (instance) CloseHandle(instance);
#endif
    return 0;
}

}  // namespace icpu::gui
