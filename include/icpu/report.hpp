#pragma once
// Informes de alto nivel que combinan metricas y estegoanalisis. Los usan la
// CLI (analyze, bench) y la GUI: aqui se decide QUE se mide; las interfaces
// solo deciden como mostrarlo.
//
//   icpu::AnalysisReport a = icpu::analyze(imagen);             // estegoanalisis
//   icpu::BenchReport    b = icpu::bench(portadora, opciones);  // experimento sistematico

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "icpu/analysis/steganalysis.hpp"
#include "icpu/image/raster.hpp"
#include "icpu/icpu.hpp"

namespace icpu {

const char* mode_name(EmbedMode mode);      // "replace" / "match"
const char* layout_name(Layout layout);     // "random" / "sequential"

// Decodifica cualquier imagen legible a pixeles (JPEG descomprimido, GIF:
// primer fotograma en RGBA), para analisis y visualizacion.
image::Raster load_pixels(ByteSpan data, std::string_view name = {});

// ---- Estegoanalisis de una imagen ----

struct AnalysisReport {
    image::ImageFormat format = image::ImageFormat::Unknown;
    image::Raster pixels;
    // En JPEG la informacion esta en los coeficientes DCT: los tests sobre pixeles
    // no tienen sentido (la descompresion lo mezcla todo). En el resto, tests espaciales.
    std::optional<analysis::ChiSquareResult> chi_square;
    std::optional<analysis::ChiSquareSweep> chi_square_sweep;
    std::optional<analysis::RsResult> rs;
    std::optional<analysis::ChiSquareResult> chi_square_dct;
};

AnalysisReport analyze(ByteSpan image, std::string_view name = {}, unsigned sweep_steps = 20);

// Interpretacion orientativa de los resultados.
const char* chi_square_verdict(double p);
const char* rs_verdict(double rate);

// ---- Experimento: distorsion y detectabilidad segun los parametros ----

struct BenchOptions {
    std::vector<double> fills{0.05, 0.1, 0.25, 0.5, 1.0};  // fracciones de la capacidad
    std::vector<unsigned> bits{1, 2};
    std::vector<EmbedMode> modes{EmbedMode::Replace, EmbedMode::Match};
    std::vector<Layout> layouts{Layout::Random, Layout::Sequential};
    std::optional<image::ImageFormat> output_format;  // por defecto, el de la portadora (o PNG)
    crypto::KdfParams kdf{1024, 1, 1};  // Argon2id rapido: el experimento no mide la KDF
};

struct Detection {
    double chi_p = -1;    // -1 = no aplica en este dominio
    double rs_rate = -1;
    double dct_p = -1;
};

struct BenchRun {
    unsigned bits = 0;
    EmbedMode mode = EmbedMode::Replace;
    Layout layout = Layout::Random;
    double fill = 0;
    std::size_t message_bytes = 0;
    std::string error;  // vacio si fue bien
    double changed_ratio = 0;  // ranuras modificadas / ranuras utiles
    double psnr = 0, ssim = 1;
    Detection detection;
    double seconds = 0;
};

struct BenchReport {
    image::ImageFormat output = image::ImageFormat::Png;
    image::Domain domain = image::Domain::Spatial;
    std::optional<Detection> baseline;  // portadora sin mensaje (si ya esta en el formato de salida)
    std::vector<BenchRun> runs;
};

// Oculta mensajes aleatorios (no comprimibles) para cada combinacion de
// parametros. LSB matching solo existe con k = 1: el resto de combinaciones se omiten.
BenchReport bench(ByteSpan cover, const BenchOptions& options, std::string_view cover_name = {},
                  const std::function<void(std::size_t done, std::size_t total)>& progress = {});

}  // namespace icpu
