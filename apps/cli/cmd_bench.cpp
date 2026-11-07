// icpu bench: experimento sistematico (ver icpu::bench en report.hpp).
// Este comando solo interpreta los argumentos y formatea los resultados.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include "common.hpp"
#include "icpu/core/file.hpp"

namespace icpu::cli {

namespace {

std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    for (std::string item; std::getline(ss, item, ',');)
        if (!item.empty()) out.push_back(item);
    require(!out.empty(), ErrorCode::InvalidArgument, "Lista vacia");
    return out;
}

double parse_fraction(const std::string& s) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (s.empty() || *end != '\0' || !(v > 0.0) || v > 1.0)
        fail(ErrorCode::InvalidArgument, "Valor invalido para --fill: " + s + " (fraccion en (0, 1])");
    return v;
}

std::string fmt(double v, const char* f) {
    if (v < 0) return "-";
    char buf[32];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

}  // namespace

int cmd_bench(const std::vector<std::string>& tokens) {
    const Args args(tokens, {"--json"});
    require(args.positionals().size() == 1, ErrorCode::InvalidArgument,
            "Uso: icpu bench <portadora> [--fill LISTA] [--bits LISTA] [--modes LISTA] [--layouts LISTA] "
            "[--format EXT] [--csv FICHERO] [--json]");
    const std::filesystem::path cover_path = utf8_path(args.positionals()[0]);
    BenchOptions opt;
    opt.fills.clear();
    opt.bits.clear();
    opt.modes.clear();
    opt.layouts.clear();
    for (const auto& s : split_list(args.value_or({"--fill"}, "0.05,0.1,0.25,0.5,1"))) opt.fills.push_back(parse_fraction(s));
    for (const auto& s : split_list(args.value_or({"--bits"}, "1,2"))) opt.bits.push_back(parse_bits(s));
    for (const auto& s : split_list(args.value_or({"--modes"}, "adaptive,replace,match"))) opt.modes.push_back(parse_mode(s));
    for (const auto& s : split_list(args.value_or({"--layouts"}, "random,sequential"))) opt.layouts.push_back(parse_layout(s));
    if (auto f = args.value({"--format"})) opt.output_format = parse_format(*f);
    const auto csv_arg = args.value({"--csv"});
    const bool as_json = args.flag({"--json"});
    args.reject_unknown();
    for (unsigned k : opt.bits) require(k != 0, ErrorCode::InvalidArgument, "--bits no admite 'auto' en bench");

    const BenchReport report = bench(read_file(cover_path), opt, path_utf8(cover_path.filename()),
                                     [&](std::size_t done, std::size_t total) {
                                         if (as_json) return;
                                         std::cerr << "\r  " << done << " / " << total << std::flush;
                                         if (done == total) std::cerr << "\n";
                                     });

    if (csv_arg) {
        std::ofstream out(utf8_path(*csv_arg), std::ios::binary);
        out << "bits,modo,orden,llenado,bytes,ranuras_cambiadas,psnr,ssim,chi2_p,rs_tasa,dct_chi2_p,segundos,error\n";
        for (const auto& r : report.runs)
            out << r.bits << "," << mode_name(r.mode) << "," << layout_name(r.layout) << "," << r.fill << ","
                << r.message_bytes << "," << r.changed_ratio << "," << r.psnr << "," << r.ssim << ","
                << r.detection.chi_p << "," << r.detection.rs_rate << "," << r.detection.dct_p << "," << r.seconds
                << ",\"" << r.error << "\"\n";
        require(bool(out), ErrorCode::Io, "No se pudo escribir el CSV");
    }

    if (as_json) {
        json::Writer w;
        w.begin_object()
            .field("cover", path_utf8(cover_path))
            .field("output", image::format_name(report.output))
            .field("domain", image::domain_name(report.domain));
        if (report.baseline)
            w.key("baseline")
                .begin_object()
                .field("chi2_p", report.baseline->chi_p)
                .field("rs_rate", report.baseline->rs_rate)
                .field("dct_chi2_p", report.baseline->dct_p)
                .end_object();
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
            w.field("changed_ratio", r.changed_ratio)
                .field("psnr", r.psnr)
                .field("ssim", r.ssim)
                .field("chi2_p", r.detection.chi_p)
                .field("rs_rate", r.detection.rs_rate)
                .field("dct_chi2_p", r.detection.dct_p)
                .field("seconds", r.seconds)
                .end_object();
        }
        w.end_array().end_object();
        std::cout << w.str() << "\n";
        return 0;
    }

    std::cout << "Portadora: " << path_utf8(cover_path) << "  ->  salida " << image::format_name(report.output)
              << " (dominio " << image::domain_name(report.domain) << ")\n";
    if (report.baseline)
        std::cout << "Referencia (portadora sin mensaje): chi2 p = " << fmt(report.baseline->chi_p, "%.3f")
                  << "  RS = " << fmt(report.baseline->rs_rate, "%.3f")
                  << "  DCT chi2 p = " << fmt(report.baseline->dct_p, "%.3f") << "\n";
    std::cout << "\n k  modo     orden       llenado     bytes  cambiadas    PSNR    SSIM  chi2 p  RS tasa  DCT p\n";
    for (const auto& r : report.runs) {
        char line[220];
        if (!r.error.empty()) {
            std::snprintf(line, sizeof line, " %u  %-7s  %-10s  %6.1f%%  %8zu  error: %s\n", r.bits, mode_name(r.mode),
                          layout_name(r.layout), r.fill * 100, r.message_bytes, r.error.c_str());
        } else {
            std::snprintf(line, sizeof line, " %u  %-7s  %-10s  %6.1f%%  %8zu  %8.3f%%  %6.2f  %6.4f  %6s  %7s  %5s\n",
                          r.bits, mode_name(r.mode), layout_name(r.layout), r.fill * 100, r.message_bytes,
                          r.changed_ratio * 100, r.psnr, r.ssim, fmt(r.detection.chi_p, "%.3f").c_str(),
                          fmt(r.detection.rs_rate, "%.3f").c_str(), fmt(r.detection.dct_p, "%.3f").c_str());
        }
        std::cout << line;
    }
    if (csv_arg) std::cout << "\nResultados guardados en " << *csv_arg << "\n";
    return 0;
}

}  // namespace icpu::cli
