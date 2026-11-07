// icpu extract: recupera el mensaje oculto de una imagen.

#include <iostream>
#include <system_error>

#include "common.hpp"
#include "console.hpp"
#include "icpu/core/file.hpp"

namespace icpu::cli {

namespace {

// El texto viene de quien creo la imagen: en una terminal, las secuencias de escape
// podrian cambiar el titulo, borrar la pantalla o falsear lo que se muestra.
std::string for_terminal(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        const bool c1 = c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) >= 0x80 &&
                        static_cast<unsigned char>(text[i + 1]) <= 0x9F;  // U+0080..U+009F en UTF-8
        if (c1) ++i;
        const bool control = (c < 0x20 && c != '\n' && c != '\r' && c != '\t') || c == 0x7F || c1;
        out += control ? '?' : text[i];
    }
    return out;
}

}  // namespace

int cmd_extract(const std::vector<std::string>& tokens) {
    const Args args(tokens, {"--force", "--json"});
    require(args.positionals().size() == 1, ErrorCode::InvalidArgument, "Uso: icpu extract <imagen> [-o SALIDA]");
    const std::filesystem::path in_path = utf8_path(args.positionals()[0]);
    const auto out = args.value({"-o", "--output"});
    const bool force = args.flag({"--force"});
    const bool as_json = args.flag({"--json"});
    ExtractOptions opt;
    opt.layout = parse_layout(args.value_or({"--layout"}, "random"));
    opt.password = resolve_password(args, false);
    args.reject_unknown();

    const Payload p = extract(read_file(in_path), opt, path_utf8(in_path.filename()));

    // Destino: -o explicito; si es un fichero oculto, su nombre original en el directorio actual.
    std::filesystem::path dest;
    if (out) dest = utf8_path(*out);
    else if (p.kind == Payload::Kind::File) dest = utf8_path(p.name.empty() ? "mensaje.bin" : p.name);
    if (!dest.empty()) {
        std::error_code ec;  // un dispositivo como NUL no admite estas consultas: no es un error
        if (std::filesystem::is_directory(dest, ec))
            dest /= utf8_path(p.name.empty() ? "mensaje.txt" : p.name);
        if (std::filesystem::exists(dest, ec) && !force)
            fail(ErrorCode::InvalidArgument, "El fichero ya existe (usa --force): " + path_utf8(dest));
        write_file(dest, p.data);
    }

    if (as_json) {
        json::Writer w;
        w.begin_object()
            .field("kind", p.kind == Payload::Kind::File ? "file" : "text")
            .field("name", p.name)
            .field("bytes", std::uint64_t(p.data.size()));
        if (!dest.empty()) w.field("saved_to", path_utf8(dest));
        else w.field("text", p.as_text());
        w.end_object();
        std::cout << w.str() << "\n";
    } else if (dest.empty()) {
        std::cout << (stdout_is_terminal() ? for_terminal(p.as_text()) : p.as_text());
        if (!p.data.empty() && p.data.back() != '\n') std::cout << "\n";
    } else {
        std::cout << (p.kind == Payload::Kind::File ? "Fichero \"" + p.name + "\"" : std::string("Texto"))
                  << " recuperado (" << human_bytes(p.data.size()) << ") -> " << path_utf8(dest) << "\n";
    }
    return 0;
}

}  // namespace icpu::cli
