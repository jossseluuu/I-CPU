#include "icpu/gif/gif.hpp"

#include <array>
#include <cstring>
#include <unordered_map>

#include "icpu/core/error.hpp"
#include "icpu/image/limits.hpp"

namespace icpu::gif {

namespace {

constexpr std::uint8_t kExtension = 0x21, kImage = 0x2C, kTrailer = 0x3B, kGraphicControl = 0xF9;
constexpr unsigned kMaxCodes = 4096;
// Un codigo de 12 bits produce como mucho 4095 pixeles: ningun GIF real expande mas.
constexpr std::size_t kMaxLzwExpansion = 2731;

[[noreturn]] void corrupt(const char* what) { fail(ErrorCode::CorruptData, std::string("GIF corrupto: ") + what); }

class Reader {
public:
    explicit Reader(ByteSpan d) : d_(d) {}
    std::uint8_t u8() {
        if (pos_ >= d_.size()) corrupt("fin de fichero inesperado");
        return d_[pos_++];
    }
    std::uint16_t u16() {
        const std::uint8_t lo = u8();
        return static_cast<std::uint16_t>(lo | (u8() << 8));
    }
    ByteSpan take(std::size_t n) {
        if (pos_ + n > d_.size()) corrupt("bloque truncado");
        ByteSpan s = d_.subspan(pos_, n);
        pos_ += n;
        return s;
    }
    // Secuencia de sub-bloques (tamano + datos) terminada en un bloque de tamano 0.
    void sub_blocks(Bytes* raw, Bytes* payload) {
        for (;;) {
            const std::uint8_t n = u8();
            if (raw) raw->push_back(n);
            if (n == 0) return;
            const ByteSpan s = take(n);
            if (raw) append(*raw, s);
            if (payload) append(*payload, s);
        }
    }
    bool done() const { return pos_ >= d_.size(); }

private:
    ByteSpan d_;
    std::size_t pos_ = 0;
};

std::size_t palette_bytes(std::uint8_t packed) { return 3u * (2u << (packed & 0x07)); }

// Escritor de codigos LZW de longitud variable, LSB primero (como exige GIF).
class CodeWriter {
public:
    void put(unsigned code, unsigned size) {
        acc_ |= std::uint32_t(code) << nbits_;
        nbits_ += size;
        while (nbits_ >= 8) {
            out_.push_back(static_cast<std::uint8_t>(acc_));
            acc_ >>= 8;
            nbits_ -= 8;
        }
    }
    Bytes finish() {
        if (nbits_ > 0) out_.push_back(static_cast<std::uint8_t>(acc_));
        return std::move(out_);
    }

private:
    Bytes out_;
    std::uint32_t acc_ = 0;
    unsigned nbits_ = 0;
};

}  // namespace

std::vector<std::uint8_t> lzw_decode(ByteSpan data, std::uint8_t min_code_size, std::size_t expected) {
    if (min_code_size < 2 || min_code_size > 8) corrupt("tamano minimo de codigo LZW invalido");
    const unsigned clear = 1u << min_code_size, eoi = clear + 1;
    std::array<std::uint16_t, kMaxCodes> prefix{};
    std::array<std::uint8_t, kMaxCodes> suffix{}, first{};
    std::array<std::uint16_t, kMaxCodes> length{};
    for (unsigned i = 0; i < clear; ++i) {
        suffix[i] = first[i] = static_cast<std::uint8_t>(i);
        length[i] = 1;
    }

    std::vector<std::uint8_t> out;
    out.reserve(expected);
    unsigned size = min_code_size + 1u, next = eoi + 1;
    int old = -1;
    std::uint32_t acc = 0;
    unsigned nbits = 0;
    std::size_t pos = 0;
    std::vector<std::uint8_t> scratch;

    auto emit = [&](unsigned code) {
        scratch.resize(length[code]);
        for (unsigned c = code, i = length[code]; i > 0; c = prefix[c]) scratch[--i] = suffix[c];
        out.insert(out.end(), scratch.begin(), scratch.end());
    };

    while (out.size() < expected) {
        while (nbits < size && pos < data.size()) {
            acc |= std::uint32_t(data[pos++]) << nbits;
            nbits += 8;
        }
        if (nbits < size) break;  // datos agotados: se rellena al final
        const unsigned code = acc & ((1u << size) - 1u);
        acc >>= size;
        nbits -= size;

        if (code == clear) {
            size = min_code_size + 1u;
            next = eoi + 1;
            old = -1;
            continue;
        }
        if (code == eoi) break;
        if (old < 0) {
            if (code >= clear) corrupt("primer codigo LZW invalido");
            emit(code);
            old = static_cast<int>(code);
            continue;
        }
        if (code > next || (code == next && next >= kMaxCodes)) corrupt("codigo LZW fuera de rango");
        const unsigned o = static_cast<unsigned>(old);
        if (next < kMaxCodes) {
            // Nueva entrada = cadena(old) + primer simbolo de la cadena actual.
            // Caso especial KwKwK: el codigo aun no existe (code == next).
            prefix[next] = static_cast<std::uint16_t>(o);
            first[next] = first[o];
            suffix[next] = code == next ? first[o] : first[code];
            length[next] = static_cast<std::uint16_t>(length[o] + 1);
            ++next;
            if (next == (1u << size) && size < 12) ++size;
        }
        emit(code);
        old = static_cast<int>(code);
    }
    out.resize(expected, 0);  // los decodificadores toleran datos cortos rellenando con 0
    return out;
}

Bytes lzw_encode(const std::vector<std::uint8_t>& indices, std::uint8_t min_code_size) {
    const unsigned clear = 1u << min_code_size, eoi = clear + 1;
    CodeWriter w;
    unsigned size = min_code_size + 1u, next = eoi + 1;
    std::unordered_map<std::uint32_t, std::uint16_t> dict;
    dict.reserve(kMaxCodes * 2);
    w.put(clear, size);
    if (indices.empty()) {
        w.put(eoi, size);
        return w.finish();
    }
    unsigned prefix = indices[0];
    for (std::size_t i = 1; i < indices.size(); ++i) {
        const std::uint8_t c = indices[i];
        const std::uint32_t key = (std::uint32_t(prefix) << 8) | c;
        const auto it = dict.find(key);
        if (it != dict.end()) {
            prefix = it->second;
            continue;
        }
        w.put(prefix, size);
        if (next < kMaxCodes) {
            if (next == (1u << size)) ++size;
            dict.emplace(key, static_cast<std::uint16_t>(next++));
        } else {
            // Tabla llena: se emite CLEAR y se reinicia el diccionario.
            w.put(clear, size);
            dict.clear();
            size = min_code_size + 1u;
            next = eoi + 1;
        }
        prefix = c;
    }
    w.put(prefix, size);
    if (next == (1u << size) && size < 12) ++size;
    w.put(eoi, size);
    return w.finish();
}

Image decode(ByteSpan file) {
    Reader r(file);
    Image img;
    const ByteSpan sig = r.take(6);
    if (std::memcmp(sig.data(), "GIF87a", 6) != 0 && std::memcmp(sig.data(), "GIF89a", 6) != 0)
        corrupt("firma invalida");
    append(img.header, sig);
    append(img.header, r.take(7));
    const std::uint8_t screen_packed = img.header[10];
    if (screen_packed & 0x80) append(img.global_palette, r.take(palette_bytes(screen_packed)));

    Bytes pending;
    int pending_transparent = -1;
    std::uint64_t total_pixels = 0;
    bool trailer = false;
    while (!r.done() && !trailer) {
        const std::uint8_t block = r.u8();
        if (block == kExtension) {
            const std::uint8_t label = r.u8();
            const std::size_t start = pending.size();
            pending.push_back(kExtension);
            pending.push_back(label);
            r.sub_blocks(&pending, nullptr);
            if (label == kGraphicControl && pending.size() - start >= 8) {
                const std::uint8_t gce_packed = pending[start + 3];
                pending_transparent = (gce_packed & 0x01) ? pending[start + 6] : -1;
            }
        } else if (block == kImage) {
            Frame f;
            f.extensions_before = std::move(pending);
            pending.clear();
            f.transparent_index = pending_transparent;
            pending_transparent = -1;
            f.left = r.u16();
            f.top = r.u16();
            f.width = r.u16();
            f.height = r.u16();
            f.packed = r.u8();
            image::check_dimensions(f.width, f.height);
            total_pixels += std::uint64_t(f.width) * f.height;
            require(total_pixels <= image::kMaxTotalPixels, ErrorCode::UnsupportedFormat,
                    "El GIF tiene demasiados pixeles en total");
            if (f.packed & 0x80) append(f.local_palette, r.take(palette_bytes(f.packed)));
            if (f.palette(img.global_palette).empty()) corrupt("fotograma sin paleta");
            f.min_code_size = r.u8();
            Bytes lzw;
            r.sub_blocks(nullptr, &lzw);
            // Se comprueba antes de reservar: un fichero minimo no puede pedir gigabytes.
            if (std::size_t(f.width) * f.height > (lzw.size() + 1) * kMaxLzwExpansion)
                corrupt("datos LZW insuficientes para las dimensiones del fotograma");
            f.indices = lzw_decode(lzw, f.min_code_size, std::size_t(f.width) * f.height);
            const std::size_t colors = f.palette(img.global_palette).size() / 3;
            for (auto& idx : f.indices)
                if (idx >= colors) idx = 0;  // indices fuera de paleta: se normalizan
            img.frames.push_back(std::move(f));
        } else if (block == kTrailer) {
            trailer = true;
        } else {
            corrupt("bloque desconocido");
        }
    }
    if (img.frames.empty()) corrupt("no contiene imagenes");
    image::check_dimensions(img.width(), img.height());
    img.trailing_blocks = std::move(pending);
    return img;
}

Bytes encode(const Image& img) {
    Bytes out = img.header;
    append(out, img.global_palette);
    for (const Frame& f : img.frames) {
        append(out, f.extensions_before);
        out.push_back(kImage);
        put_u16le(out, f.left);
        put_u16le(out, f.top);
        put_u16le(out, f.width);
        put_u16le(out, f.height);
        out.push_back(f.packed);
        append(out, f.local_palette);
        out.push_back(f.min_code_size);
        const Bytes lzw = lzw_encode(f.indices, f.min_code_size);
        for (std::size_t off = 0; off < lzw.size(); off += 255) {
            const std::size_t n = std::min<std::size_t>(255, lzw.size() - off);
            out.push_back(static_cast<std::uint8_t>(n));
            out.insert(out.end(), lzw.begin() + static_cast<std::ptrdiff_t>(off),
                       lzw.begin() + static_cast<std::ptrdiff_t>(off + n));
        }
        out.push_back(0);
    }
    append(out, img.trailing_blocks);
    out.push_back(kTrailer);
    return out;
}

image::Raster first_frame_rgba(const Image& img) {
    image::Raster r(img.width(), img.height(), 4, 8);
    const Frame& f = img.frames.front();
    const Bytes& pal = f.palette(img.global_palette);
    // Los GIF entrelazados almacenan las filas en 4 pasadas: se reordenan.
    const bool interlaced = (f.packed & 0x40) != 0;
    std::vector<std::uint32_t> row_of(f.height);
    if (interlaced) {
        std::uint32_t i = 0;
        for (std::uint32_t y = 0; y < f.height; y += 8) row_of[i++] = y;
        for (std::uint32_t y = 4; y < f.height; y += 8) row_of[i++] = y;
        for (std::uint32_t y = 2; y < f.height; y += 4) row_of[i++] = y;
        for (std::uint32_t y = 1; y < f.height; y += 2) row_of[i++] = y;
    } else {
        for (std::uint32_t y = 0; y < f.height; ++y) row_of[y] = y;
    }
    for (std::uint32_t sy = 0; sy < f.height; ++sy) {
        const std::uint32_t y = f.top + row_of[sy];
        if (y >= r.height) continue;
        for (std::uint32_t x = 0; x < f.width; ++x) {
            const std::uint32_t px = f.left + x;
            if (px >= r.width) continue;
            const std::uint8_t idx = f.indices[std::size_t(sy) * f.width + x];
            const std::size_t p = std::size_t(y) * r.width + px;
            if (idx == f.transparent_index) continue;
            for (unsigned c = 0; c < 3; ++c) r.at(p, c) = pal[idx * 3u + c];
            r.at(p, 3) = 255;
        }
    }
    return r;
}

}  // namespace icpu::gif
