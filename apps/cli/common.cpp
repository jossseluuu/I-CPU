#include "common.hpp"

#include <cstdlib>
#include <cstdio>
#include <iostream>

#include "console.hpp"
#include "icpu/core/file.hpp"

namespace icpu::cli {

std::string resolve_password(const Args& args, bool confirm) {
    if (auto file = args.value({"--password-file"})) {
        std::string s = to_string(read_file(utf8_path(*file)));
        if (s.rfind("\xEF\xBB\xBF", 0) == 0) s.erase(0, 3);  // BOM de editores de Windows
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (const auto nl = s.find_first_of("\r\n"); nl != std::string::npos) s.resize(nl);
        require(!s.empty(), ErrorCode::InvalidArgument, "El fichero de contrasena esta vacio");
        return normalize_password(s);
    }
    if (auto p = args.value({"-p", "--password"})) {
        std::cerr << "Aviso: pasar la contrasena por argumento la deja en el historial de la terminal; "
                     "usa --password-file o escribela cuando se pida.\n";
        return normalize_password(*p);
    }
    if (auto env = environment_utf8("ICPU_PASSWORD")) return normalize_password(*env);

    require(stdin_is_terminal(), ErrorCode::InvalidArgument,
            "No hay contrasena: usa --password-file, -p o la variable ICPU_PASSWORD");
    std::string pass = read_hidden("Contrasena: ");
    require(!pass.empty(), ErrorCode::InvalidArgument, "La contrasena no puede estar vacia");
    if (confirm && read_hidden("Repite la contrasena: ") != pass)
        fail(ErrorCode::InvalidArgument, "Las contrasenas no coinciden");
    return normalize_password(pass);
}

Layout parse_layout(const std::string& s) {
    if (s == "random" || s == "aleatorio") return Layout::Random;
    if (s == "sequential" || s == "secuencial") return Layout::Sequential;
    fail(ErrorCode::InvalidArgument, "Orden desconocido: " + s + " (usa random o sequential)");
}

EmbedMode parse_mode(const std::string& s) {
    if (s == "replace" || s == "sustitucion") return EmbedMode::Replace;
    if (s == "match" || s == "matching") return EmbedMode::Match;
    if (s == "adaptive" || s == "adaptativo") return EmbedMode::Adaptive;
    fail(ErrorCode::InvalidArgument, "Modo desconocido: " + s + " (usa adaptive, match o replace)");
}

unsigned parse_bits(const std::string& s) {
    if (s == "auto") return 0;
    return static_cast<unsigned>(parse_int(s, 1, 4, "--bits"));
}

image::ImageFormat parse_format(const std::string& s) {
    const image::ImageFormat f = image::format_from_extension(s);
    if (f == image::ImageFormat::Unknown || !image::can_write(f))
        fail(ErrorCode::InvalidArgument, "Formato de salida no soportado: " + s);
    return f;
}

int parse_int(const std::string& s, int lo, int hi, const char* what) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end != '\0' || v < lo || v > hi)
        fail(ErrorCode::InvalidArgument, std::string("Valor invalido para ") + what + ": " + s + " (rango " +
                                             std::to_string(lo) + ".." + std::to_string(hi) + ")");
    return static_cast<int>(v);
}

std::string human_bytes(std::uint64_t n) {
    char buf[32];
    if (n < 1024) std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(n));
    else if (n < 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f KiB", double(n) / 1024);
    else std::snprintf(buf, sizeof buf, "%.2f MiB", double(n) / (1024.0 * 1024.0));
    return buf;
}

std::string percent(double ratio) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f %%", ratio * 100.0);
    return buf;
}

}  // namespace icpu::cli
