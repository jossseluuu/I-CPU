// icpu embed: oculta un texto o un fichero dentro de una imagen.

#include <chrono>
#include <iostream>

#include "common.hpp"
#include "console.hpp"
#include "icpu/core/file.hpp"

namespace icpu::cli {

int cmd_embed(const std::vector<std::string>& tokens) {
    const Args args(tokens, {"--stdin", "--no-compress", "--no-verify", "--force", "--json"});
    require(args.positionals().size() == 1, ErrorCode::InvalidArgument,
            "Uso: icpu embed <portadora> -o <salida> (-m TEXTO | -f FICHERO | --stdin)");
    const std::filesystem::path cover_path = utf8_path(args.positionals()[0]);
    const auto out = args.value({"-o", "--output"});
    require(out.has_value(), ErrorCode::InvalidArgument, "Falta la imagen de salida (-o)");
    const std::filesystem::path out_path = utf8_path(*out);

    // Que se oculta: exactamente una de las tres fuentes.
    const auto message = args.value({"-m", "--message"});
    const auto file = args.value({"-f", "--file"});
    const bool from_stdin = args.flag({"--stdin"});
    require(int(message.has_value()) + int(file.has_value()) + int(from_stdin) == 1, ErrorCode::InvalidArgument,
            "Indica exactamente uno de: -m TEXTO, -f FICHERO o --stdin");
    Payload payload;
    if (message) payload = Payload::text(*message);
    else if (file) payload = Payload::file(path_utf8(utf8_path(*file).filename()), read_file(utf8_path(*file)));
    else payload = Payload::text(read_all_stdin());

    EmbedOptions opt;
    opt.bits_per_slot = parse_bits(args.value_or({"--bits"}, "auto"));
    opt.mode = parse_mode(args.value_or({"--mode"}, "replace"));
    opt.layout = parse_layout(args.value_or({"--layout"}, "random"));
    opt.jpeg_quality = parse_int(args.value_or({"--quality"}, "90"), 1, 100, "--quality");
    opt.compress = !args.flag({"--no-compress"});
    opt.verify = !args.flag({"--no-verify"});
    if (auto f = args.value({"--format"})) opt.output_format = parse_format(*f);
    else if (const auto f2 = image::format_from_extension(lower_extension(out_path)); f2 != image::ImageFormat::Unknown)
        opt.output_format = parse_format(lower_extension(out_path));
    const bool force = args.flag({"--force"});
    const bool as_json = args.flag({"--json"});
    opt.password = resolve_password(args, true);
    args.reject_unknown();

    if (std::filesystem::exists(out_path) && !force)
        fail(ErrorCode::InvalidArgument, "La salida ya existe (usa --force para sobrescribir): " + path_utf8(out_path));
    if (opt.layout == Layout::Sequential)
        std::cerr << "Aviso: el orden secuencial es solo para experimentos; es trivialmente detectable.\n";

    const Bytes cover = read_file(cover_path);
    const auto t0 = std::chrono::steady_clock::now();
    const EmbedResult r = embed(cover, payload, opt, path_utf8(cover_path.filename()));
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    write_file(out_path, r.image);

    const double fill = r.capacity_bytes ? double(r.packed_bytes) / double(r.capacity_bytes) : 0;
    if (as_json) {
        json::Writer w;
        w.begin_object()
            .field("output", path_utf8(out_path))
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
            .field("seconds", secs)
            .key("notes")
            .begin_array();
        for (const auto& n : r.notes) w.value(n);
        w.end_array().end_object();
        std::cout << w.str() << "\n";
        return 0;
    }

    std::cout << "Mensaje ocultado en " << path_utf8(out_path) << "\n"
              << "  Formato / dominio : " << image::format_name(r.format) << " / " << image::domain_name(r.domain) << "\n"
              << "  Bits por ranura   : " << r.bits_per_slot << "\n"
              << "  Mensaje           : " << human_bytes(r.message_bytes)
              << (r.compressed ? " (comprimido a " + human_bytes(r.packed_bytes) + ")" : std::string()) << "\n"
              << "  Capacidad usada   : " << percent(fill) << " de " << human_bytes(r.capacity_bytes) << "\n"
              << "  Ranuras escritas  : " << r.stats.slots_used << " (cambiaron " << r.stats.slots_changed << ")\n"
              << "  Verificacion      : " << (r.verified ? "correcta" : "omitida") << "\n"
              << "  Tamano del fichero: " << human_bytes(cover.size()) << " -> " << human_bytes(r.image.size()) << "\n"
              << "  Tiempo            : " << secs << " s\n";
    for (const auto& n : r.notes) std::cout << "  Nota: " << n << "\n";
    return 0;
}

}  // namespace icpu::cli
