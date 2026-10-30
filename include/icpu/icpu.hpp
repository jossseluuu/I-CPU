#pragma once
// API de alto nivel de I-CPU: lo unico que necesitan las interfaces (CLI, GUI)
// y quien quiera usar I-CPU como biblioteca.
//
//   icpu::EmbedResult r = icpu::embed(cover_bytes, icpu::Payload::text("hola"), opts);
//   icpu::Payload p     = icpu::extract(r.image, {.password = "clave"});

#include <optional>
#include <string>
#include <vector>

#include "icpu/core/bytes.hpp"
#include "icpu/core/error.hpp"
#include "icpu/crypto/kdf.hpp"
#include "icpu/image/format.hpp"
#include "icpu/stego/engine.hpp"
#include "icpu/stego/payload.hpp"

namespace icpu {

using stego::EmbedMode;
using stego::Layout;
using stego::Payload;

const char* version();

struct EmbedOptions {
    std::string password;
    unsigned bits_per_slot = 0;  // 0 = automatico: el minimo que permita que quepa el mensaje
    EmbedMode mode = EmbedMode::Replace;
    Layout layout = Layout::Random;
    std::optional<image::ImageFormat> output_format;  // por defecto, el de la portadora (o PNG)
    int jpeg_quality = 90;  // solo si hay que comprimir a JPEG una portadora que no lo es
    bool compress = true;
    bool verify = true;  // relee la imagen generada y comprueba que el mensaje se recupera
    crypto::KdfParams kdf{};
};

struct EmbedResult {
    Bytes image;
    image::ImageFormat format = image::ImageFormat::Unknown;
    image::Domain domain = image::Domain::Spatial;
    unsigned bits_per_slot = 1;
    std::size_t message_bytes = 0;   // tamano original del texto/fichero
    std::size_t packed_bytes = 0;    // tras empaquetar (y comprimir)
    bool compressed = false;
    std::size_t capacity_bytes = 0;  // maximo empaquetado con ese k
    std::uint64_t usable_slots = 0;
    stego::EmbedStats stats;
    bool verified = false;
    std::vector<std::string> notes;
};

EmbedResult embed(ByteSpan cover, const Payload& payload, const EmbedOptions& options,
                  std::string_view cover_name = {});

struct ExtractOptions {
    std::string password;
    Layout layout = Layout::Random;
    crypto::KdfParams kdf{};
};

Payload extract(ByteSpan stego_image, const ExtractOptions& options, std::string_view name = {});

struct CapacityEntry {
    image::ImageFormat output = image::ImageFormat::Png;
    image::Domain domain = image::Domain::Spatial;
    unsigned bits_per_slot = 1;
    std::uint64_t usable_slots = 0;
    std::size_t message_bytes = 0;  // texto maximo aproximado (sin compresion)
};

struct ImageInfo {
    image::ImageFormat format = image::ImageFormat::Unknown;
    std::uint32_t width = 0, height = 0;
    unsigned channels = 0, depth = 0;
    std::size_t file_bytes = 0;
    unsigned frames = 1;
    int jpeg_quality = -1;
    bool jpeg_progressive = false;
    std::string jpeg_subsampling;
    std::vector<CapacityEntry> capacities;
};

ImageInfo inspect(ByteSpan file, std::string_view name = {});

}  // namespace icpu
