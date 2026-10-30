// icpu info: caracteristicas de una imagen y capacidad en cada dominio.
// icpu formats: tabla de formatos soportados.

#include <cstdio>
#include <iostream>

#include "common.hpp"
#include "icpu/core/file.hpp"

namespace icpu::cli {

int cmd_info(const std::vector<std::string>& tokens) {
    const Args args(tokens, {"--json"});
    require(args.positionals().size() == 1, ErrorCode::InvalidArgument, "Uso: icpu info <imagen> [--json]");
    const bool as_json = args.flag({"--json"});
    args.reject_unknown();
    const std::filesystem::path path = utf8_path(args.positionals()[0]);
    const ImageInfo info = inspect(read_file(path), path_utf8(path.filename()));

    if (as_json) {
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
                .field("domain", image::domain_name(c.domain))
                .field("bits_per_slot", c.bits_per_slot)
                .field("usable_slots", c.usable_slots)
                .field("message_bytes", std::uint64_t(c.message_bytes))
                .end_object();
        w.end_array().end_object();
        std::cout << w.str() << "\n";
        return 0;
    }

    std::cout << "Imagen     : " << path_utf8(path) << "\n"
              << "Formato    : " << image::format_name(info.format) << "\n"
              << "Dimensiones: " << info.width << " x " << info.height << " px, " << info.channels
              << " canal(es), " << info.depth << " bits\n"
              << "Tamano     : " << human_bytes(info.file_bytes) << "\n";
    if (info.frames > 1) std::cout << "Fotogramas : " << info.frames << "\n";
    if (info.jpeg_quality >= 0)
        std::cout << "JPEG       : calidad estimada " << info.jpeg_quality
                  << (info.jpeg_progressive ? ", progresivo" : ", secuencial")
                  << (info.jpeg_subsampling.empty() ? "" : ", croma " + info.jpeg_subsampling) << "\n";
    std::cout << "\nCapacidad maxima (mensaje sin comprimir):\n"
              << "  Salida  Dominio   Bits/ranura  Ranuras utiles   Mensaje maximo\n";
    for (const auto& c : info.capacities) {
        char line[160];
        std::snprintf(line, sizeof line, "  %-6s  %-8s  %11u  %14llu   %s\n", image::format_name(c.output),
                      image::domain_name(c.domain), c.bits_per_slot, static_cast<unsigned long long>(c.usable_slots),
                      human_bytes(c.message_bytes).c_str());
        std::cout << line;
    }
    return 0;
}

int cmd_formats(const std::vector<std::string>&) {
    std::cout << "Formato  Lectura  Escritura  Dominio de ocultacion  Notas\n"
                 "PNG      si       si         espacial (LSB 1-4 bits) 8/16 bits, conserva metadatos (ICC, EXIF, texto)\n"
                 "BMP      si       si         espacial (LSB 1-4 bits) 24/32 bits\n"
                 "TGA      si       si         espacial (LSB 1-4 bits) compresion RLE sin perdida\n"
                 "PNM      si       si         espacial (LSB 1-4 bits) PBM/PGM/PPM, ASCII y binario\n"
                 "QOI      si       si         espacial (LSB 1-4 bits) formato moderno sin perdida\n"
                 "JPEG     si       si         frecuencial (DCT)       baseline, progresivo y aritmetico\n"
                 "GIF      si       si         paleta (EzStego)        conserva animacion y transparencia\n"
                 "PSD      si       -> PNG     espacial                imagen compuesta\n"
                 "HDR      si       -> PNG     espacial                convertida a 8 bits\n"
                 "PIC      si       -> PNG     espacial                Softimage PIC\n";
    return 0;
}

}  // namespace icpu::cli
