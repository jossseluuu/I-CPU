#include "icpu/report.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>

#include "icpu/analysis/metrics.hpp"
#include "icpu/crypto/random.hpp"
#include "icpu/gif/gif.hpp"
#include "icpu/image/codec.hpp"
#include "icpu/jpeg/jpeg.hpp"

namespace icpu {

namespace {

image::ImageFormat detect(ByteSpan data, std::string_view name) {
    const auto f = image::detect_format(data, std::filesystem::path(std::string(name)).extension().string());
    require(f != image::ImageFormat::Unknown, ErrorCode::UnsupportedFormat,
            "Formato de imagen no reconocido o no soportado");
    return f;
}

Detection detect_stego(ByteSpan file, const image::Raster& pixels, image::ImageFormat format) {
    Detection d;
    if (format == image::ImageFormat::Jpeg) {
        d.dct_p = analysis::chi_square_dct(jpeg::read_coefficients(file)).p_value;
    } else {
        d.chi_p = analysis::chi_square_image(pixels).p_value;
        d.rs_rate = analysis::rs_analysis(pixels).estimated_rate;
    }
    return d;
}

}  // namespace

const char* mode_name(EmbedMode mode) {
    switch (mode) {
        case EmbedMode::Match: return "match";
        case EmbedMode::Adaptive: return "adaptive";
        default: return "replace";
    }
}
const char* layout_name(Layout layout) { return layout == Layout::Sequential ? "sequential" : "random"; }

image::Raster load_pixels(ByteSpan data, std::string_view name) {
    const auto f = detect(data, name);
    if (f == image::ImageFormat::Jpeg) return jpeg::decompress_pixels(data);
    if (f == image::ImageFormat::Gif) return gif::first_frame_rgba(gif::decode(data));
    return image::decode_raster(data, f);
}

AnalysisReport analyze(ByteSpan image, std::string_view name, unsigned sweep_steps) {
    AnalysisReport r;
    r.format = detect(image, name);
    r.pixels = load_pixels(image, name);
    if (r.format == image::ImageFormat::Jpeg) {
        r.chi_square_dct = analysis::chi_square_dct(jpeg::read_coefficients(image));
    } else {
        r.chi_square = analysis::chi_square_image(r.pixels);
        r.chi_square_sweep = analysis::chi_square_sweep(r.pixels, sweep_steps);
        r.rs = analysis::rs_analysis(r.pixels);
    }
    return r;
}

const char* chi_square_verdict(double p) {
    if (p > 0.95) return "insercion muy probable";
    if (p > 0.5) return "dudoso";
    return "sin indicios";
}

const char* rs_verdict(double rate) {
    if (rate > 0.10) return "insercion probable";
    if (rate > 0.03) return "dudoso";
    return "sin indicios";
}

BenchReport bench(ByteSpan cover, const BenchOptions& opt, std::string_view cover_name,
                  const std::function<void(std::size_t, std::size_t)>& progress) {
    for (unsigned k : opt.bits)
        require(k >= 1 && k <= 4, ErrorCode::InvalidArgument, "Los bits por ranura del experimento deben ser 1..4");
    for (double f : opt.fills)
        require(f > 0.0 && f <= 1.0, ErrorCode::InvalidArgument, "El llenado debe ser una fraccion en (0, 1]");

    const ImageInfo info = inspect(cover, cover_name);
    BenchReport report;
    report.output = opt.output_format.value_or(image::can_write(info.format) ? info.format : image::ImageFormat::Png);
    report.domain = image::domain_for_output(report.output);
    const image::Raster cover_pixels = load_pixels(cover, cover_name);
    if (info.format == report.output) report.baseline = detect_stego(cover, cover_pixels, report.output);
    const std::string stego_name = std::string("x.") + image::default_extension(report.output);

    // Combinaciones validas (LSB matching y el modo adaptativo solo con k = 1)
    for (unsigned k : opt.bits)
        for (EmbedMode mode : opt.modes) {
            if (mode != EmbedMode::Replace && k > 1) continue;
            for (Layout layout : opt.layouts)
                for (double fill : opt.fills) {
                    BenchRun run;
                    run.bits = k;
                    run.mode = mode;
                    run.layout = layout;
                    run.fill = fill;
                    report.runs.push_back(run);
                }
        }

    for (std::size_t i = 0; i < report.runs.size(); ++i) {
        BenchRun& run = report.runs[i];
        if (progress) progress(i, report.runs.size());

        const CapacityEntry* cap = nullptr;
        for (const auto& c : info.capacities)
            if (c.output == report.output && c.bits_per_slot == run.bits) cap = &c;
        if (!cap) {
            run.error = "k no soportado en este dominio";
            continue;
        }
        const std::size_t capacity = run.mode == EmbedMode::Adaptive ? cap->adaptive_bytes : cap->message_bytes;
        run.message_bytes = std::max<std::size_t>(1, std::size_t(run.fill * double(capacity)));
        std::string message(run.message_bytes, '\0');
        crypto::os_random(reinterpret_cast<std::uint8_t*>(message.data()), message.size());

        EmbedOptions eo;
        eo.password = "icpu-bench";
        eo.bits_per_slot = run.bits;
        eo.mode = run.mode;
        eo.layout = run.layout;
        eo.output_format = report.output;
        eo.compress = false;
        eo.kdf = opt.kdf;
        try {
            const auto t0 = std::chrono::steady_clock::now();
            const EmbedResult r = embed(cover, Payload::text(message), eo, cover_name);
            run.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            const image::Raster stego = load_pixels(r.image, stego_name);
            const analysis::QualityMetrics q = analysis::compare(cover_pixels, stego);
            run.changed_ratio = r.usable_slots ? double(r.stats.slots_changed) / double(r.usable_slots) : 0;
            run.psnr = q.psnr;
            run.ssim = q.ssim;
            run.detection = detect_stego(r.image, stego, report.output);
        } catch (const Error& e) {
            run.error = e.what();
        }
    }
    if (progress) progress(report.runs.size(), report.runs.size());
    return report;
}

}  // namespace icpu
