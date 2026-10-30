#include "icpu/analysis/steganalysis.hpp"

#include <algorithm>
#include <cmath>

namespace icpu::analysis {

namespace {

// Funcion gamma incompleta regularizada P(a, x) (Numerical Recipes, 6.2).
double gamma_p(double a, double x) {
    if (x <= 0) return 0.0;
    const double gln = std::lgamma(a);
    if (x < a + 1) {
        double sum = 1.0 / a, del = sum, ap = a;
        for (int n = 0; n < 1000; ++n) {
            ap += 1;
            del *= x / ap;
            sum += del;
            if (std::fabs(del) < std::fabs(sum) * 1e-15) break;
        }
        return sum * std::exp(-x + a * std::log(x) - gln);
    }
    // Fraccion continua para Q(a, x) = 1 - P(a, x) (algoritmo de Lentz).
    double b = x + 1 - a, c = 1.0 / 1e-300, d = 1.0 / b, h = d;
    for (int i = 1; i < 1000; ++i) {
        const double an = -i * (i - a);
        b += 2;
        d = an * d + b;
        if (std::fabs(d) < 1e-300) d = 1e-300;
        c = b + an / c;
        if (std::fabs(c) < 1e-300) c = 1e-300;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < 1e-15) break;
    }
    return 1.0 - std::exp(-x + a * std::log(x) - gln) * h;
}

std::vector<std::uint64_t> color_histogram(const image::Raster& r, std::size_t max_pixels) {
    std::vector<std::uint64_t> h(r.depth == 16 ? 65536 : 256, 0);
    const unsigned cc = r.color_channels();
    const std::size_t n = std::min(max_pixels, r.pixel_count());
    for (std::size_t p = 0; p < n; ++p)
        for (unsigned c = 0; c < cc; ++c) ++h[r.at(p, c)];
    return h;
}

// Grupos de 4 muestras horizontales y mascara [0 1 1 0].
constexpr int kMask[4] = {0, 1, 1, 0};

int flip_pos(int x) { return x ^ 1; }                  // F1:  0<->1, 2<->3, ...
int flip_neg(int x) { return ((x + 1) ^ 1) - 1; }      // F-1: -1<->0, 1<->2, ...

int smoothness(const int* g) {
    return std::abs(g[1] - g[0]) + std::abs(g[2] - g[1]) + std::abs(g[3] - g[2]);
}

struct RsCounts {
    double rm = 0, sm = 0, r_m = 0, s_m = 0;
};

RsCounts rs_counts(const image::Raster& r, bool flip_all) {
    RsCounts out;
    std::uint64_t groups = 0;
    const unsigned cc = r.color_channels();
    for (unsigned c = 0; c < cc; ++c)
        for (std::uint32_t y = 0; y < r.height; ++y)
            for (std::uint32_t x = 0; x + 4 <= r.width; x += 4) {
                int g[4], gp[4], gn[4];
                for (int i = 0; i < 4; ++i) {
                    int v = r.at(std::size_t(y) * r.width + x + static_cast<unsigned>(i), c);
                    if (flip_all) v = flip_pos(v);
                    g[i] = v;
                    gp[i] = kMask[i] ? flip_pos(v) : v;
                    gn[i] = kMask[i] ? flip_neg(v) : v;
                }
                const int f0 = smoothness(g), fp = smoothness(gp), fn = smoothness(gn);
                out.rm += fp > f0;
                out.sm += fp < f0;
                out.r_m += fn > f0;
                out.s_m += fn < f0;
                ++groups;
            }
    if (groups) {
        out.rm /= double(groups);
        out.sm /= double(groups);
        out.r_m /= double(groups);
        out.s_m /= double(groups);
    }
    return out;
}

}  // namespace

double chi_square_cdf(double x, double k) { return gamma_p(k / 2.0, x / 2.0); }

ChiSquareResult chi_square_pairs(const std::vector<std::uint64_t>& h) {
    ChiSquareResult res;
    double chi = 0;
    unsigned categories = 0;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2) {
        const double expected = (double(h[i]) + double(h[i + 1])) / 2.0;
        if (expected < 5) continue;  // regla habitual: categorias con frecuencia esperada >= 5
        const double d = double(h[i]) - expected;
        chi += d * d / expected;
        ++categories;
    }
    res.statistic = chi;
    res.degrees_of_freedom = categories > 1 ? categories - 1 : 1;
    res.p_value = categories > 1 ? 1.0 - chi_square_cdf(chi, res.degrees_of_freedom) : 0.0;
    return res;
}

ChiSquareResult chi_square_image(const image::Raster& raster) {
    return chi_square_pairs(color_histogram(raster, raster.pixel_count()));
}

ChiSquareSweep chi_square_sweep(const image::Raster& raster, unsigned steps) {
    ChiSquareSweep s;
    for (unsigned i = 1; i <= steps; ++i) {
        const double frac = double(i) / steps;
        const auto n = static_cast<std::size_t>(frac * double(raster.pixel_count()));
        s.fraction.push_back(frac);
        s.p_value.push_back(chi_square_pairs(color_histogram(raster, n)).p_value);
    }
    return s;
}

RsResult rs_analysis(const image::Raster& raster) {
    const image::Raster r = image::convert_depth(raster, 8);
    const RsCounts a = rs_counts(r, false);  // imagen tal cual: tasa p/2
    const RsCounts b = rs_counts(r, true);   // todos los LSB volteados: tasa 1 - p/2
    RsResult res{0, a.rm, a.sm, a.r_m, a.s_m};
    const double d0 = a.rm - a.sm, d1 = b.rm - b.sm;
    const double dn0 = a.r_m - a.s_m, dn1 = b.r_m - b.s_m;
    // 2(d1 + d0) z^2 + (dn0 - dn1 - d1 - 3 d0) z + (d0 - dn0) = 0 ;  p = z / (z - 1/2)
    const double qa = 2 * (d1 + d0), qb = dn0 - dn1 - d1 - 3 * d0, qc = d0 - dn0;
    double z = 0;
    if (std::fabs(qa) < 1e-12) {
        z = std::fabs(qb) > 1e-12 ? -qc / qb : 0;
    } else {
        const double disc = qb * qb - 4 * qa * qc;
        if (disc >= 0) {
            const double z1 = (-qb + std::sqrt(disc)) / (2 * qa), z2 = (-qb - std::sqrt(disc)) / (2 * qa);
            z = std::fabs(z1) < std::fabs(z2) ? z1 : z2;
        }
    }
    const double p = std::fabs(z - 0.5) > 1e-12 ? z / (z - 0.5) : 0;
    res.estimated_rate = std::min(1.0, std::max(0.0, p));  // max(0.0, -0.0) = +0.0 (clamp conservaria -0.0)
    return res;
}

ChiSquareResult chi_square_dct(const jpeg::CoefImage& img) {
    // Histograma de coeficientes AC en [-512, 511], desplazado para que los pares
    // (2m, 2m+1) caigan en bins consecutivos; se excluye el par {0, 1} (JSteg no lo usa).
    std::vector<std::uint64_t> h(1024, 0);
    for (const auto& comp : img.components)
        for (std::size_t i = 0; i < comp.coefs.size(); ++i) {
            if (i % 64 == 0) continue;
            const int c = comp.coefs[i];
            if (c >= -512 && c < 512 && (c >> 1) != 0) ++h[static_cast<std::size_t>(c + 512)];
        }
    return chi_square_pairs(h);
}

}  // namespace icpu::analysis
