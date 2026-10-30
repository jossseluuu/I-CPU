// analysis: metricas de distorsion, estegoanalisis y visualizaciones.

#include <doctest.h>

#include <cmath>

#include "helpers.hpp"
#include "icpu/analysis/metrics.hpp"
#include "icpu/analysis/steganalysis.hpp"
#include "icpu/analysis/visual.hpp"
#include "icpu/image/png.hpp"
#include "icpu/jpeg/jpeg.hpp"
#include "icpu/report.hpp"

using namespace icpu;
using image::Raster;

namespace {

// Sustitucion LSB directa sobre una fraccion de las muestras (sin pasar por el motor).
Raster lsb_replace(Raster r, double fraction, std::uint32_t seed = 3) {
    test::Rng rng(seed);
    for (auto& s : r.samples)
        if (double(rng.next() % 10000) < fraction * 10000) s = static_cast<std::uint16_t>((s & ~1u) | (rng.next() & 1u));
    return r;
}

}  // namespace

TEST_SUITE("analysis") {

TEST_CASE("Metricas: imagenes identicas y diferencia unitaria") {
    const Raster a = test::natural_raster(64, 64);
    const auto same = analysis::compare(a, a);
    CHECK(same.mse == 0);
    CHECK(std::isinf(same.psnr));
    CHECK(same.ssim == doctest::Approx(1.0));
    CHECK(same.changed_samples == 0);

    Raster flat(32, 32, 3, 8), plus1(32, 32, 3, 8);
    for (auto& s : flat.samples) s = 100;
    for (auto& s : plus1.samples) s = 101;
    const auto m = analysis::compare(flat, plus1);
    CHECK(m.mse == doctest::Approx(1.0));
    CHECK(m.psnr == doctest::Approx(10 * std::log10(255.0 * 255.0)).epsilon(1e-6));  // 48.13 dB
    CHECK(m.max_abs_diff == 1);
    CHECK(m.changed_ratio() == doctest::Approx(1.0));

    CHECK(test::error_of([&] { analysis::compare(a, test::natural_raster(32, 64)); }).has_value());
}

TEST_CASE("Chi-cuadrado: funcion de distribucion") {
    // k = 2: CDF(x) = 1 - e^(-x/2)
    CHECK(analysis::chi_square_cdf(2.0, 2.0) == doctest::Approx(1 - std::exp(-1.0)).epsilon(1e-6));
    CHECK(analysis::chi_square_cdf(0.0, 5.0) == doctest::Approx(0.0));
    CHECK(analysis::chi_square_cdf(1000.0, 5.0) == doctest::Approx(1.0));
}

TEST_CASE("Chi-cuadrado y RS distinguen la sustitucion LSB") {
    const Raster cover = test::natural_raster(256, 256);
    const auto chi_cover = analysis::chi_square_image(cover);
    const auto rs_cover = analysis::rs_analysis(cover);
    CHECK(chi_cover.p_value < 0.5);
    CHECK(rs_cover.estimated_rate < 0.08);

    const Raster full = lsb_replace(cover, 1.0);
    CHECK(analysis::chi_square_image(full).p_value > 0.9);

    // Con la mitad de las muestras usadas, RS estima ~0.5
    const auto rs_half = analysis::rs_analysis(lsb_replace(cover, 0.5));
    CHECK(rs_half.estimated_rate > 0.3);
    CHECK(rs_half.estimated_rate < 0.7);

    // Insercion secuencial en la primera mitad: el barrido cae donde acaba el mensaje
    Raster seq = cover;
    const Raster replaced = lsb_replace(cover, 1.0);
    std::copy(replaced.samples.begin(), replaced.samples.begin() + std::ptrdiff_t(seq.samples.size() / 2),
              seq.samples.begin());
    const auto sweep = analysis::chi_square_sweep(seq, 10);
    REQUIRE(sweep.p_value.size() == 10);
    CHECK(sweep.fraction.back() == doctest::Approx(1.0));
    CHECK(sweep.p_value[1] > 0.9);   // 20 %: dentro del mensaje
    CHECK(sweep.p_value[9] < sweep.p_value[1]);
}

TEST_CASE("Chi-cuadrado DCT detecta JSteg (LSB de coeficientes AC)") {
    const Bytes file = jpeg::compress_pixels(test::natural_raster(256, 256), 90);
    jpeg::CoefImage coefs = jpeg::read_coefficients(file);
    const double p_clean = analysis::chi_square_dct(coefs).p_value;
    test::Rng rng(4);
    for (auto& comp : coefs.components)
        for (std::size_t i = 0; i < comp.coefs.size(); ++i) {
            auto& v = comp.coefs[i];
            if (i % 64 != 0 && v != 0 && v != 1) v = static_cast<std::int16_t>((v & ~1) | int(rng.next() & 1u));
        }
    CHECK(p_clean < 0.5);
    CHECK(analysis::chi_square_dct(coefs).p_value > 0.9);
}

TEST_CASE("Visualizaciones: plano de bits, mapa de diferencias e histogramas") {
    Raster r(4, 1, 3, 8);
    const std::uint16_t vals[] = {0, 1, 2, 3};
    for (std::size_t p = 0; p < 4; ++p)
        for (unsigned c = 0; c < 3; ++c) r.at(p, c) = vals[p];
    const Raster b0 = analysis::bit_plane(r, 0);
    CHECK(b0.at(0, 0) == 0);
    CHECK(b0.at(1, 0) == 255);
    CHECK(b0.at(3, 2) == 255);
    const Raster b1 = analysis::bit_plane(r, 1, 0);
    CHECK(b1.channels == 1);
    CHECK(b1.at(2, 0) == 255);
    CHECK(b1.at(1, 0) == 0);

    Raster s = r;
    s.at(0, 0) = 1;
    const Raster d = analysis::difference_map(r, s, 64);
    CHECK(d.at(0, 0) == 64);
    CHECK(d.at(1, 0) == 0);

    const auto h = analysis::histograms(r);
    REQUIRE(h.size() == 3);
    CHECK(h[0][2] == 1);
    CHECK(h[1][0] == 1);
}

}  // TEST_SUITE analysis

TEST_SUITE("report") {

TEST_CASE("analyze: tests espaciales en PNG y DCT en JPEG") {
    const Bytes png = image::png_encode(test::natural_raster(64, 48));
    const AnalysisReport a = analyze(png, "c.png", 5);
    CHECK(a.format == image::ImageFormat::Png);
    CHECK(a.pixels.width == 64);
    CHECK(a.chi_square.has_value());
    CHECK(a.rs.has_value());
    REQUIRE(a.chi_square_sweep.has_value());
    CHECK(a.chi_square_sweep->p_value.size() == 5);
    CHECK_FALSE(a.chi_square_dct.has_value());

    const AnalysisReport j = analyze(jpeg::compress_pixels(test::natural_raster(64, 48), 90), "c.jpg");
    CHECK(j.chi_square_dct.has_value());
    CHECK_FALSE(j.rs.has_value());
    CHECK(j.pixels.channels == 3);

    CHECK(test::error_of([] { analyze(as_bytes("texto"), "x.txt"); }) == ErrorCode::UnsupportedFormat);
}

TEST_CASE("bench: combinaciones validas, referencia y metricas") {
    const Bytes cover = image::png_encode(test::natural_raster(64, 48));
    BenchOptions o;
    o.fills = {0.5};
    o.layouts = {Layout::Random};  // bits {1,2} x modos {replace, match}: match solo con k = 1
    std::size_t last_done = 0, calls = 0;
    const BenchReport r = bench(cover, o, "c.png", [&](std::size_t done, std::size_t total) {
        CHECK(total == 3);
        last_done = done;
        ++calls;
    });
    CHECK(calls == 4);
    CHECK(last_done == 3);
    CHECK(r.output == image::ImageFormat::Png);
    CHECK(r.baseline.has_value());
    REQUIRE(r.runs.size() == 3);
    for (const auto& run : r.runs) {
        CAPTURE(run.error);
        CHECK(run.error.empty());
        CHECK(run.psnr > 40);
        CHECK(run.detection.rs_rate >= 0);
        CHECK(run.detection.dct_p < 0);  // no aplica en PNG
    }
    CHECK(r.runs[0].bits == 1);
    CHECK(r.runs[1].mode == EmbedMode::Match);
    CHECK(r.runs[2].bits == 2);

    o.bits = {5};
    CHECK(test::error_of([&] { bench(cover, o, "c.png"); }) == ErrorCode::InvalidArgument);
}

}  // TEST_SUITE report
