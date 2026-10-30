// icpu analyze: estegoanalisis de una imagen y, si se da la portadora
// original, metricas de distorsion. Puede exportar visualizaciones.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>

#include "common.hpp"
#include "icpu/analysis/metrics.hpp"
#include "icpu/analysis/visual.hpp"
#include "icpu/core/file.hpp"
#include "icpu/image/codec.hpp"

namespace icpu::cli {

namespace {

void write_png(const std::filesystem::path& path, const image::Raster& r) {
    write_file(path, image::encode_raster(r, image::ImageFormat::Png));
}

}  // namespace

int cmd_analyze(const std::vector<std::string>& tokens) {
    const Args args(tokens, {"--json"});
    require(args.positionals().size() == 1, ErrorCode::InvalidArgument,
            "Uso: icpu analyze <imagen> [--cover ORIGINAL] [--export DIR] [--steps N] [--json]");
    const std::filesystem::path path = utf8_path(args.positionals()[0]);
    const auto cover_arg = args.value({"--cover"});
    const auto export_arg = args.value({"--export"});
    const unsigned steps = static_cast<unsigned>(parse_int(args.value_or({"--steps"}, "20"), 1, 100, "--steps"));
    const bool as_json = args.flag({"--json"});
    args.reject_unknown();

    const AnalysisReport report = analyze(read_file(path), path_utf8(path.filename()), steps);
    const image::ImageFormat format = report.format;
    const image::Raster& pixels = report.pixels;
    const auto& chi = report.chi_square;
    const auto& sweep = report.chi_square_sweep;
    const auto& rs = report.rs;
    const auto& dct = report.chi_square_dct;

    std::optional<analysis::QualityMetrics> quality;
    std::optional<image::Raster> cover_pixels;
    if (cover_arg) {
        const std::filesystem::path cover_path = utf8_path(*cover_arg);
        cover_pixels = load_pixels(read_file(cover_path), path_utf8(cover_path.filename()));
        require(cover_pixels->width == pixels.width && cover_pixels->height == pixels.height,
                ErrorCode::InvalidArgument, "La portadora y la imagen deben tener las mismas dimensiones");
        quality = analysis::compare(*cover_pixels, pixels);
    }

    std::vector<std::string> exported;
    if (export_arg) {
        const std::filesystem::path dir = utf8_path(*export_arg);
        std::filesystem::create_directories(dir);
        for (unsigned bit = 0; bit < 2; ++bit) {
            const auto p = dir / ("plano_bit" + std::to_string(bit) + ".png");
            write_png(p, analysis::bit_plane(pixels, bit));
            exported.push_back(path_utf8(p));
        }
        if (cover_pixels) {
            const auto p = dir / "diferencias.png";
            write_png(p, analysis::difference_map(*cover_pixels, pixels));
            exported.push_back(path_utf8(p));
        }
        {
            const auto p = dir / "histograma.csv";
            const auto h = analysis::histograms(pixels);
            std::ofstream out(p, std::ios::binary);
            out << "valor";
            for (std::size_t c = 0; c < h.size(); ++c) out << ",canal" << c;
            out << "\n";
            for (unsigned v = 0; v < 256; ++v) {
                out << v;
                for (const auto& ch : h) out << "," << ch[v];
                out << "\n";
            }
            require(bool(out), ErrorCode::Io, "No se pudo escribir el histograma");
            exported.push_back(path_utf8(p));
        }
        if (sweep) {
            const auto p = dir / "barrido_chi2.csv";
            std::ofstream out(p, std::ios::binary);
            out << "fraccion,p\n";
            for (std::size_t i = 0; i < sweep->fraction.size(); ++i)
                out << sweep->fraction[i] << "," << sweep->p_value[i] << "\n";
            require(bool(out), ErrorCode::Io, "No se pudo escribir el barrido chi-cuadrado");
            exported.push_back(path_utf8(p));
        }
    }

    if (as_json) {
        json::Writer w;
        w.begin_object()
            .field("image", path_utf8(path))
            .field("format", image::format_name(format))
            .field("width", pixels.width)
            .field("height", pixels.height);
        if (chi)
            w.key("chi_square")
                .begin_object()
                .field("statistic", chi->statistic)
                .field("dof", chi->degrees_of_freedom)
                .field("p", chi->p_value)
                .end_object();
        if (sweep) {
            w.key("chi_square_sweep").begin_array();
            for (std::size_t i = 0; i < sweep->fraction.size(); ++i)
                w.begin_object().field("fraction", sweep->fraction[i]).field("p", sweep->p_value[i]).end_object();
            w.end_array();
        }
        if (rs)
            w.key("rs")
                .begin_object()
                .field("estimated_rate", rs->estimated_rate)
                .field("rm", rs->rm)
                .field("sm", rs->sm)
                .field("r_m", rs->r_m)
                .field("s_m", rs->s_m)
                .end_object();
        if (dct)
            w.key("chi_square_dct")
                .begin_object()
                .field("statistic", dct->statistic)
                .field("dof", dct->degrees_of_freedom)
                .field("p", dct->p_value)
                .end_object();
        if (quality)
            w.key("quality")
                .begin_object()
                .field("mse", quality->mse)
                .field("psnr", quality->psnr)
                .field("ssim", quality->ssim)
                .field("changed_ratio", quality->changed_ratio())
                .field("max_abs_diff", quality->max_abs_diff)
                .end_object();
        w.key("exported").begin_array();
        for (const auto& e : exported) w.value(e);
        w.end_array().end_object();
        std::cout << w.str() << "\n";
        return 0;
    }

    char line[200];
    std::cout << "Imagen : " << path_utf8(path) << " (" << image::format_name(format) << ", " << pixels.width
              << " x " << pixels.height << ")\n\nEstegoanalisis:\n";
    if (chi) {
        std::snprintf(line, sizeof line, "  Chi-cuadrado (imagen completa): p = %.4f  -> %s\n", chi->p_value,
                      chi_square_verdict(chi->p_value));
        std::cout << line;
    }
    if (rs) {
        std::snprintf(line, sizeof line, "  Analisis RS: tasa estimada = %s  -> %s\n",
                      percent(rs->estimated_rate).c_str(), rs_verdict(rs->estimated_rate));
        std::cout << line;
    }
    if (sweep) {
        std::cout << "  Barrido chi-cuadrado (p por porcion analizada):\n    ";
        for (std::size_t i = 0; i < sweep->fraction.size(); ++i) {
            std::snprintf(line, sizeof line, "%3.0f%%:%.2f ", sweep->fraction[i] * 100, sweep->p_value[i]);
            std::cout << line << ((i + 1) % 8 == 0 && i + 1 < sweep->fraction.size() ? "\n    " : "");
        }
        std::cout << "\n";
    }
    if (dct) {
        std::snprintf(line, sizeof line, "  Chi-cuadrado DCT (JSteg): p = %.4f  -> %s\n", dct->p_value,
                      chi_square_verdict(dct->p_value));
        std::cout << line;
    }
    if (format == image::ImageFormat::Gif)
        std::cout << "  Nota: en GIF los datos van en los indices de paleta; los tests LSB sobre colores\n"
                     "        son solo orientativos.\n";
    std::cout << "  (Los tests clasicos detectan bien la sustitucion LSB; LSB matching y el orden\n"
                 "   aleatorio con poca carga estan pensados precisamente para resistirlos.)\n";

    if (quality) {
        std::cout << "\nDistorsion respecto a la portadora:\n";
        std::snprintf(line, sizeof line,
                      "  MSE = %.5f   PSNR = %.2f dB   SSIM = %.5f\n  Muestras cambiadas = %s   Diferencia maxima = %u\n",
                      quality->mse, quality->psnr, quality->ssim, percent(quality->changed_ratio()).c_str(),
                      quality->max_abs_diff);
        std::cout << line;
    }
    if (!exported.empty()) {
        std::cout << "\nFicheros exportados:\n";
        for (const auto& e : exported) std::cout << "  " << e << "\n";
    }
    return 0;
}

}  // namespace icpu::cli
