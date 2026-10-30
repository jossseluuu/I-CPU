#include "icpu/jpeg/jpeg.hpp"

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// jpeglib.h necesita size_t y FILE declarados antes.
#include <jpeglib.h>

#include "icpu/core/error.hpp"

#if defined(_MSC_VER)
// C4324: jmp_buf exige alineacion y rellena ErrorManager (inofensivo).
// C4611: setjmp + destructores C++; las funciones *_impl solo tienen tipos triviales en su ambito.
#  pragma warning(disable : 4324 4611)
#endif

namespace icpu::jpeg {

namespace {

// libjpeg informa de los errores llamando a error_exit, que no debe volver.
// Se usa setjmp/longjmp (patron estandar de libjpeg) y despues se traduce a
// una excepcion de C++ ya fuera del codigo C.
struct ErrorManager {
    jpeg_error_mgr pub;
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};

void on_error(j_common_ptr cinfo) {
    auto* err = reinterpret_cast<ErrorManager*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, err->message);
    std::longjmp(err->jump, 1);
}

void on_message(j_common_ptr) {}  // silencia los avisos por stderr

void init_errors(ErrorManager& err) {
    jpeg_std_error(&err.pub);
    err.pub.error_exit = on_error;
    err.pub.output_message = on_message;
    err.message[0] = '\0';
}

// Calidad 50 de la tabla de luminancia estandar (Anexo K.1), orden natural.
constexpr std::uint8_t kStdLuma[64] = {
    16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
    14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
    18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};

void save_all_markers(j_decompress_ptr d) {
    jpeg_save_markers(d, JPEG_COM, 0xFFFF);
    for (int m = 0; m < 16; ++m) jpeg_save_markers(d, JPEG_APP0 + m, 0xFFFF);
}

// Copia los metadatos del original, salvo los que libjpeg escribe por si mismo
// (cabecera JFIF y marcador Adobe), igual que hace jpegtran.
void copy_markers(j_decompress_ptr src, j_compress_ptr dst) {
    for (jpeg_saved_marker_ptr m = src->marker_list; m != nullptr; m = m->next) {
        if (dst->write_JFIF_header && m->marker == JPEG_APP0 && m->data_length >= 5 &&
            std::memcmp(m->data, "JFIF", 5) == 0)
            continue;
        if (dst->write_Adobe_marker && m->marker == JPEG_APP0 + 14 && m->data_length >= 5 &&
            std::memcmp(m->data, "Adobe", 5) == 0)
            continue;
        jpeg_write_marker(dst, m->marker, m->data, m->data_length);
    }
}

// ---- Funciones con setjmp: solo tipos triviales en su ambito (sin destructores C++) ----

bool read_impl(const unsigned char* data, unsigned long size, CoefImage* out, char* msg) {
    jpeg_decompress_struct d;
    ErrorManager err;
    init_errors(err);
    d.err = &err.pub;
    if (setjmp(err.jump)) {
        std::strncpy(msg, err.message, JMSG_LENGTH_MAX - 1);
        jpeg_destroy_decompress(&d);
        return false;
    }
    jpeg_create_decompress(&d);
    jpeg_mem_src(&d, data, size);
    jpeg_read_header(&d, TRUE);
    jvirt_barray_ptr* arrays = jpeg_read_coefficients(&d);

    out->width = d.image_width;
    out->height = d.image_height;
    out->progressive = d.progressive_mode != 0;
    out->arithmetic = d.arith_code != 0;
    out->components.resize(static_cast<std::size_t>(d.num_components));
    for (int c = 0; c < d.num_components; ++c) {
        const jpeg_component_info& ci = d.comp_info[c];
        Component& comp = out->components[static_cast<std::size_t>(c)];
        comp.id = ci.component_id;
        comp.h = ci.h_samp_factor;
        comp.v = ci.v_samp_factor;
        comp.blocks_w = ci.width_in_blocks;
        comp.blocks_h = ci.height_in_blocks;
        if (ci.quant_table)
            for (int k = 0; k < 64; ++k) comp.qtable[k] = ci.quant_table->quantval[k];
        comp.coefs.resize(std::size_t(comp.blocks_w) * comp.blocks_h * 64);
        for (JDIMENSION by = 0; by < ci.height_in_blocks; ++by) {
            JBLOCKARRAY row = (*d.mem->access_virt_barray)(reinterpret_cast<j_common_ptr>(&d), arrays[c], by, 1, FALSE);
            for (JDIMENSION bx = 0; bx < ci.width_in_blocks; ++bx)
                std::memcpy(&comp.coefs[(std::size_t(by) * comp.blocks_w + bx) * 64], row[0][bx], 64 * sizeof(JCOEF));
        }
    }
    jpeg_finish_decompress(&d);
    jpeg_destroy_decompress(&d);
    return true;
}

bool write_impl(const unsigned char* data, unsigned long size, const CoefImage* mod, unsigned char** out_buf,
                unsigned long* out_size, char* msg) {
    jpeg_decompress_struct d;
    jpeg_compress_struct c;
    // Un unico gestor para ambos objetos: libjpeg solo informa de un error a la vez,
    // y asi basta un setjmp (encadenarlos con || seria comportamiento indefinido).
    ErrorManager err;
    init_errors(err);
    d.err = &err.pub;
    c.err = &err.pub;
    volatile bool compress_created = false;  // se lee tras longjmp: debe ser volatile
    if (setjmp(err.jump)) {
        std::strncpy(msg, err.message, JMSG_LENGTH_MAX - 1);
        if (compress_created) jpeg_destroy_compress(&c);
        jpeg_destroy_decompress(&d);
        return false;
    }
    jpeg_create_decompress(&d);
    jpeg_mem_src(&d, data, size);
    save_all_markers(&d);
    jpeg_read_header(&d, TRUE);
    jvirt_barray_ptr* arrays = jpeg_read_coefficients(&d);

    if (static_cast<std::size_t>(d.num_components) != mod->components.size()) {
        std::strncpy(msg, "estructura de componentes distinta", JMSG_LENGTH_MAX - 1);
        jpeg_destroy_decompress(&d);
        return false;
    }
    // Se sobrescriben los coeficientes en los arrays virtuales del decodificador.
    for (int ci = 0; ci < d.num_components; ++ci) {
        const Component& comp = mod->components[static_cast<std::size_t>(ci)];
        for (JDIMENSION by = 0; by < d.comp_info[ci].height_in_blocks; ++by) {
            JBLOCKARRAY row = (*d.mem->access_virt_barray)(reinterpret_cast<j_common_ptr>(&d), arrays[ci], by, 1, TRUE);
            for (JDIMENSION bx = 0; bx < d.comp_info[ci].width_in_blocks; ++bx)
                std::memcpy(row[0][bx], &comp.coefs[(std::size_t(by) * comp.blocks_w + bx) * 64], 64 * sizeof(JCOEF));
        }
    }

    jpeg_create_compress(&c);
    compress_created = true;
    jpeg_mem_dest(&c, out_buf, out_size);
    jpeg_copy_critical_parameters(&d, &c);
    c.optimize_coding = TRUE;  // tablas Huffman a medida: los nuevos simbolos siempre tienen codigo
    if (d.arith_code) c.arith_code = TRUE;
    if (d.progressive_mode) jpeg_simple_progression(&c);
    jpeg_write_coefficients(&c, arrays);
    copy_markers(&d, &c);
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    jpeg_finish_decompress(&d);
    jpeg_destroy_decompress(&d);
    return true;
}

bool compress_impl(const unsigned char* px, JDIMENSION w, JDIMENSION h, int comps, int quality,
                   unsigned char** out_buf, unsigned long* out_size, char* msg) {
    jpeg_compress_struct c;
    ErrorManager err;
    init_errors(err);
    c.err = &err.pub;
    if (setjmp(err.jump)) {
        std::strncpy(msg, err.message, JMSG_LENGTH_MAX - 1);
        jpeg_destroy_compress(&c);
        return false;
    }
    jpeg_create_compress(&c);
    jpeg_mem_dest(&c, out_buf, out_size);
    c.image_width = w;
    c.image_height = h;
    c.input_components = comps;
    c.in_color_space = comps == 1 ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_set_defaults(&c);
    jpeg_set_quality(&c, quality, TRUE);
    c.optimize_coding = TRUE;
    jpeg_start_compress(&c, TRUE);
    const std::size_t stride = std::size_t(w) * static_cast<std::size_t>(comps);
    while (c.next_scanline < c.image_height) {
        JSAMPROW row = const_cast<JSAMPROW>(px + c.next_scanline * stride);
        jpeg_write_scanlines(&c, &row, 1);
    }
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    return true;
}

bool decompress_impl(const unsigned char* data, unsigned long size, unsigned char** px, JDIMENSION* w,
                     JDIMENSION* h, int* comps, char* msg) {
    jpeg_decompress_struct d;
    ErrorManager err;
    init_errors(err);
    d.err = &err.pub;
    *px = nullptr;
    if (setjmp(err.jump)) {
        std::strncpy(msg, err.message, JMSG_LENGTH_MAX - 1);
        std::free(*px);
        *px = nullptr;
        jpeg_destroy_decompress(&d);
        return false;
    }
    jpeg_create_decompress(&d);
    jpeg_mem_src(&d, data, size);
    jpeg_read_header(&d, TRUE);
    d.out_color_space = d.num_components == 1 ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_start_decompress(&d);
    *w = d.output_width;
    *h = d.output_height;
    *comps = d.output_components;
    const std::size_t stride = std::size_t(d.output_width) * static_cast<std::size_t>(d.output_components);
    *px = static_cast<unsigned char*>(std::malloc(stride * d.output_height));
    if (*px == nullptr) {
        std::strncpy(msg, "memoria insuficiente", JMSG_LENGTH_MAX - 1);
        jpeg_destroy_decompress(&d);
        return false;
    }
    while (d.output_scanline < d.output_height) {
        JSAMPROW row = *px + d.output_scanline * stride;
        jpeg_read_scanlines(&d, &row, 1);
    }
    jpeg_finish_decompress(&d);
    jpeg_destroy_decompress(&d);
    return true;
}

[[noreturn]] void libjpeg_fail(const char* what, const char* msg) {
    fail(ErrorCode::CorruptData, std::string(what) + ": " + msg);
}

}  // namespace

CoefImage read_coefficients(ByteSpan file) {
    CoefImage img;
    char msg[JMSG_LENGTH_MAX] = {};
    if (!read_impl(file.data(), static_cast<unsigned long>(file.size()), &img, msg))
        libjpeg_fail("No se pueden leer los coeficientes JPEG", msg);
    return img;
}

Bytes write_coefficients(ByteSpan original, const CoefImage& modified) {
    unsigned char* buf = nullptr;
    unsigned long size = 0;
    char msg[JMSG_LENGTH_MAX] = {};
    const bool ok = write_impl(original.data(), static_cast<unsigned long>(original.size()), &modified, &buf, &size, msg);
    Bytes out;
    if (ok && buf) out.assign(buf, buf + size);
    std::free(buf);
    if (!ok) libjpeg_fail("No se puede escribir el JPEG", msg);
    return out;
}

Bytes compress_pixels(const image::Raster& raster, int quality) {
    require(raster.depth == 8 && (raster.channels == 1 || raster.channels == 3), ErrorCode::Internal,
            "compress_pixels requiere gris o RGB de 8 bits");
    std::vector<unsigned char> px(raster.samples.size());
    for (std::size_t i = 0; i < px.size(); ++i) px[i] = static_cast<unsigned char>(raster.samples[i]);
    unsigned char* buf = nullptr;
    unsigned long size = 0;
    char msg[JMSG_LENGTH_MAX] = {};
    const bool ok = compress_impl(px.data(), raster.width, raster.height, raster.channels,
                                  std::clamp(quality, 1, 100), &buf, &size, msg);
    Bytes out;
    if (ok && buf) out.assign(buf, buf + size);
    std::free(buf);
    if (!ok) libjpeg_fail("No se puede comprimir a JPEG", msg);
    return out;
}

image::Raster decompress_pixels(ByteSpan file) {
    unsigned char* px = nullptr;
    JDIMENSION w = 0, h = 0;
    int comps = 0;
    char msg[JMSG_LENGTH_MAX] = {};
    if (!decompress_impl(file.data(), static_cast<unsigned long>(file.size()), &px, &w, &h, &comps, msg))
        libjpeg_fail("No se puede decodificar el JPEG", msg);
    image::Raster r(w, h, static_cast<std::uint8_t>(comps), 8);
    for (std::size_t i = 0; i < r.samples.size(); ++i) r.samples[i] = px[i];
    std::free(px);
    return r;
}

int estimate_quality(const CoefImage& img) {
    if (img.components.empty()) return 0;
    const auto& q = img.components[0].qtable;
    int best_q = 50;
    double best_err = 1e300;
    for (int quality = 1; quality <= 100; ++quality) {
        const int scale = quality < 50 ? 5000 / quality : 200 - 2 * quality;
        double err = 0;
        for (int k = 0; k < 64; ++k) {
            const int v = std::clamp((kStdLuma[k] * scale + 50) / 100, 1, 255);
            const double diff = double(v) - double(q[k]);
            err += diff * diff;
        }
        if (err < best_err) {
            best_err = err;
            best_q = quality;
        }
    }
    return best_q;
}

}  // namespace icpu::jpeg
