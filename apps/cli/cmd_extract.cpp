// icpu extract: recupera el mensaje oculto de una imagen.

#include <iostream>

#include "common.hpp"
#include "icpu/core/file.hpp"

namespace icpu::cli {

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
        if (std::filesystem::is_directory(dest))
            dest /= utf8_path(p.name.empty() ? "mensaje.txt" : p.name);
        if (std::filesystem::exists(dest) && !force)
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
        std::cout << p.as_text();
        if (!p.data.empty() && p.data.back() != '\n') std::cout << "\n";
    } else {
        std::cout << (p.kind == Payload::Kind::File ? "Fichero \"" + p.name + "\"" : std::string("Texto"))
                  << " recuperado (" << human_bytes(p.data.size()) << ") -> " << path_utf8(dest) << "\n";
    }
    return 0;
}

}  // namespace icpu::cli
