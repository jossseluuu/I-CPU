#include "icpu/image/png.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <set>

#include "icpu/compress/zlib.hpp"
#include "icpu/core/error.hpp"

namespace icpu::image {

namespace {

constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

const std::array<std::uint32_t, 256>& crc_table() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

void write_chunk(Bytes& out, std::string_view type, ByteSpan data) {
    put_u32be(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    append(out, as_bytes(type));
    append(out, data);
    put_u32be(out, crc32(ByteSpan(out).subspan(start)));
}

std::uint8_t color_type_for(std::uint8_t channels) {
    switch (channels) {
        case 1: return 0;  // gris
        case 2: return 4;  // gris + alfa
        case 3: return 2;  // RGB
        default: return 6; // RGBA
    }
}

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

// Filtra una fila con el filtro `type` (0..4) segun la especificacion PNG.
void filter_row(int type, const std::uint8_t* cur, const std::uint8_t* prev, std::size_t len, std::size_t bpp,
                std::uint8_t* out) {
    for (std::size_t i = 0; i < len; ++i) {
        const int a = i >= bpp ? cur[i - bpp] : 0;
        const int b = prev ? prev[i] : 0;
        const int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
        int pred = 0;
        switch (type) {
            case 1: pred = a; break;
            case 2: pred = b; break;
            case 3: pred = (a + b) / 2; break;
            case 4: pred = paeth(a, b, c); break;
            default: pred = 0; break;
        }
        out[i] = static_cast<std::uint8_t>(cur[i] - pred);
    }
}

}  // namespace

std::uint32_t crc32(ByteSpan data, std::uint32_t crc) {
    const auto& t = crc_table();
    crc = ~crc;
    for (std::uint8_t b : data) crc = t[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

std::vector<PngChunk> png_read_chunks(ByteSpan file) {
    require(file.size() >= 8 && std::memcmp(file.data(), kSignature, 8) == 0, ErrorCode::CorruptData,
            "Firma PNG invalida");
    std::vector<PngChunk> chunks;
    std::size_t pos = 8;
    while (pos + 12 <= file.size()) {
        const std::uint32_t len = get_u32be(file.data() + pos);
        require(len <= file.size() - pos - 12, ErrorCode::CorruptData, "Chunk PNG truncado");
        PngChunk c;
        c.type.assign(reinterpret_cast<const char*>(file.data() + pos + 4), 4);
        c.data.assign(file.begin() + pos + 8, file.begin() + pos + 8 + len);
        pos += 12 + len;
        const bool end = c.type == "IEND";
        chunks.push_back(std::move(c));
        if (end) break;
    }
    return chunks;
}

PngMetadata png_read_metadata(ByteSpan file) {
    PngMetadata meta;
    for (auto& c : png_read_chunks(file)) {
        if (c.type == "IHDR" && c.data.size() >= 10) {
            meta.bit_depth = c.data[8];
            meta.color_type = c.data[9];
        }
        // Los chunks auxiliares tienen la primera letra en minuscula.
        if (c.type.size() == 4 && (c.type[0] & 0x20)) meta.ancillary.push_back(std::move(c));
    }
    return meta;
}

std::vector<PngChunk> png_chunks_to_preserve(const PngMetadata& source, const Raster& output) {
    // Chunks que describen la interpretacion del color o son metadatos: siguen
    // siendo correctos porque solo se alteran los bits menos significativos.
    static const std::set<std::string> kAlwaysKeep = {"gAMA", "cHRM", "sRGB", "iCCP", "cICP", "mDCV", "mDCv",
                                                      "cLLI", "cLLi", "pHYs", "tEXt", "zTXt", "iTXt", "tIME",
                                                      "eXIf", "sPLT", "oFFs", "pCAL", "sCAL"};
    // Dependen del tipo de color / profundidad exactos.
    static const std::set<std::string> kKeepIfSameLayout = {"sBIT", "bKGD"};
    // Nunca: transparencia por clave de color (se convirtio a alfa), histograma
    // de paleta y chunks de animacion APNG (solo se procesa el primer fotograma).
    static const std::set<std::string> kNeverKeep = {"tRNS", "hIST", "acTL", "fcTL", "fdAT"};

    const bool same_layout =
        source.color_type == color_type_for(output.channels) && source.bit_depth == output.depth;
    std::vector<PngChunk> keep;
    for (const auto& c : source.ancillary) {
        if (kNeverKeep.count(c.type)) continue;
        const bool safe_to_copy = (c.type[3] & 0x20) != 0;
        if (kAlwaysKeep.count(c.type) || (kKeepIfSameLayout.count(c.type) && same_layout) ||
            (!kKeepIfSameLayout.count(c.type) && safe_to_copy))
            keep.push_back(c);
    }
    return keep;
}

Bytes png_encode(const Raster& r, const std::vector<PngChunk>& extra_chunks) {
    require(r.width > 0 && r.height > 0 && r.channels >= 1 && r.channels <= 4, ErrorCode::InvalidArgument,
            "Imagen vacia o con canales invalidos");
    require(r.depth == 8 || r.depth == 16, ErrorCode::InvalidArgument, "Profundidad PNG no soportada");

    const std::size_t bytes_per_sample = r.depth / 8u;
    const std::size_t bpp = r.channels * bytes_per_sample;
    const std::size_t row_len = std::size_t(r.width) * bpp;

    // Serializa las muestras (big-endian para 16 bits) y aplica filtrado adaptativo.
    Bytes filtered;
    filtered.reserve((row_len + 1) * r.height);
    Bytes prev_row, cur_row(row_len), candidate(row_len), best(row_len);
    for (std::uint32_t y = 0; y < r.height; ++y) {
        const std::uint16_t* src = r.samples.data() + std::size_t(y) * r.width * r.channels;
        for (std::size_t i = 0; i < std::size_t(r.width) * r.channels; ++i) {
            if (bytes_per_sample == 1) {
                cur_row[i] = static_cast<std::uint8_t>(src[i]);
            } else {
                cur_row[2 * i] = static_cast<std::uint8_t>(src[i] >> 8);
                cur_row[2 * i + 1] = static_cast<std::uint8_t>(src[i]);
            }
        }
        std::uint64_t best_score = UINT64_MAX;
        int best_type = 0;
        for (int type = 0; type <= 4; ++type) {
            filter_row(type, cur_row.data(), y ? prev_row.data() : nullptr, row_len, bpp, candidate.data());
            std::uint64_t score = 0;
            for (std::uint8_t v : candidate) score += static_cast<std::uint64_t>(std::abs(static_cast<std::int8_t>(v)));
            if (score < best_score) {
                best_score = score;
                best_type = type;
                best.swap(candidate);
            }
        }
        filtered.push_back(static_cast<std::uint8_t>(best_type));
        append(filtered, best);
        prev_row = cur_row;
    }

    Bytes out(kSignature, kSignature + 8);
    Bytes ihdr;
    put_u32be(ihdr, r.width);
    put_u32be(ihdr, r.height);
    ihdr.push_back(r.depth);
    ihdr.push_back(color_type_for(r.channels));
    ihdr.push_back(0);  // compresion: deflate
    ihdr.push_back(0);  // filtrado: adaptativo
    ihdr.push_back(0);  // sin entrelazado
    write_chunk(out, "IHDR", ihdr);
    for (const auto& c : extra_chunks) write_chunk(out, c.type, c.data);

    const Bytes z = compress::zlib_compress(filtered, 8);
    constexpr std::size_t kMaxIdat = 1u << 20;
    for (std::size_t off = 0; off < z.size(); off += kMaxIdat) {
        const std::size_t n = std::min(kMaxIdat, z.size() - off);
        write_chunk(out, "IDAT", ByteSpan(z).subspan(off, n));
    }
    write_chunk(out, "IEND", {});
    return out;
}

}  // namespace icpu::image
