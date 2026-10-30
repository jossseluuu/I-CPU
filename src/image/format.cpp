#include "icpu/image/format.hpp"

#include <cstring>
#include <string>

#include <stb_image.h>

namespace icpu::image {

namespace {
bool starts_with(ByteSpan data, const char* magic, std::size_t n) {
    return data.size() >= n && std::memcmp(data.data(), magic, n) == 0;
}
}  // namespace

ImageFormat detect_format(ByteSpan data, std::string_view extension_hint) {
    if (starts_with(data, "\x89PNG\r\n\x1a\n", 8)) return ImageFormat::Png;
    if (starts_with(data, "\xFF\xD8\xFF", 3)) return ImageFormat::Jpeg;
    if (starts_with(data, "GIF87a", 6) || starts_with(data, "GIF89a", 6)) return ImageFormat::Gif;
    if (starts_with(data, "BM", 2)) return ImageFormat::Bmp;
    if (starts_with(data, "qoif", 4)) return ImageFormat::Qoi;
    if (starts_with(data, "8BPS", 4)) return ImageFormat::Psd;
    if (starts_with(data, "#?RADIANCE", 10) || starts_with(data, "#?RGBE", 6)) return ImageFormat::Hdr;
    if (starts_with(data, "\x53\x80\xF6\x34", 4)) return ImageFormat::Pic;
    if (data.size() >= 2 && data[0] == 'P' && data[1] >= '1' && data[1] <= '6') return ImageFormat::Pnm;

    // TGA no tiene numero magico: se acepta por extension o si stb lo reconoce.
    if (format_from_extension(extension_hint) == ImageFormat::Tga) return ImageFormat::Tga;
    int w = 0, h = 0, c = 0;
    if (data.size() < 0x7FFFFFFF &&
        stbi_info_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &c))
        return ImageFormat::Tga;
    return ImageFormat::Unknown;
}

ImageFormat format_from_extension(std::string_view ext) {
    std::string e(ext);
    for (char& ch : e) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    if (!e.empty() && e.front() == '.') e.erase(0, 1);
    if (e == "png") return ImageFormat::Png;
    if (e == "jpg" || e == "jpeg" || e == "jpe" || e == "jfif") return ImageFormat::Jpeg;
    if (e == "gif") return ImageFormat::Gif;
    if (e == "bmp" || e == "dib") return ImageFormat::Bmp;
    if (e == "tga" || e == "tpic") return ImageFormat::Tga;
    if (e == "pnm" || e == "ppm" || e == "pgm" || e == "pbm") return ImageFormat::Pnm;
    if (e == "qoi") return ImageFormat::Qoi;
    if (e == "psd") return ImageFormat::Psd;
    if (e == "hdr") return ImageFormat::Hdr;
    if (e == "pic") return ImageFormat::Pic;
    return ImageFormat::Unknown;
}

const char* format_name(ImageFormat f) {
    switch (f) {
        case ImageFormat::Png: return "PNG";
        case ImageFormat::Jpeg: return "JPEG";
        case ImageFormat::Gif: return "GIF";
        case ImageFormat::Bmp: return "BMP";
        case ImageFormat::Tga: return "TGA";
        case ImageFormat::Pnm: return "PNM";
        case ImageFormat::Qoi: return "QOI";
        case ImageFormat::Psd: return "PSD";
        case ImageFormat::Hdr: return "HDR";
        case ImageFormat::Pic: return "PIC";
        case ImageFormat::Unknown: break;
    }
    return "desconocido";
}

const char* default_extension(ImageFormat f) {
    switch (f) {
        case ImageFormat::Png: return "png";
        case ImageFormat::Jpeg: return "jpg";
        case ImageFormat::Gif: return "gif";
        case ImageFormat::Bmp: return "bmp";
        case ImageFormat::Tga: return "tga";
        case ImageFormat::Pnm: return "ppm";
        case ImageFormat::Qoi: return "qoi";
        default: return "png";
    }
}

const char* domain_name(Domain d) {
    switch (d) {
        case Domain::Spatial: return "espacial";
        case Domain::Dct: return "dct";
        case Domain::Palette: return "paleta";
    }
    return "?";
}

bool can_write(ImageFormat f) {
    switch (f) {
        case ImageFormat::Png:
        case ImageFormat::Jpeg:
        case ImageFormat::Gif:
        case ImageFormat::Bmp:
        case ImageFormat::Tga:
        case ImageFormat::Pnm:
        case ImageFormat::Qoi: return true;
        default: return false;
    }
}

bool is_lossy(ImageFormat f) { return f == ImageFormat::Jpeg; }

Domain domain_for_output(ImageFormat f) {
    if (f == ImageFormat::Jpeg) return Domain::Dct;
    if (f == ImageFormat::Gif) return Domain::Palette;
    return Domain::Spatial;
}

}  // namespace icpu::image
