// Objetivo de libFuzzer: todo lo que procesa una imagen de origen desconocido.
// El primer byte elige el formato declarado por el nombre del fichero.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

#include "icpu/report.hpp"
#include "icpu/icpu.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 2) return 0;
    static constexpr std::string_view kNames[] = {"x.png", "x.jpg", "x.gif", "x.bmp", "x.tga", "x.qoi", "x.ppm"};
    const std::string_view name = kNames[data[0] % std::size(kNames)];
    const icpu::ByteSpan file(data + 1, size - 1);

    try {
        icpu::inspect(file, name);
    } catch (const icpu::Error&) {
    }
    try {
        icpu::analyze(file, name, 4);
    } catch (const icpu::Error&) {
    }
    try {
        icpu::ExtractOptions options;
        options.password = "fuzz";
        options.kdf = {8, 1, 1};
        icpu::extract(file, options, name);
    } catch (const icpu::Error&) {
    }
    return 0;
}
