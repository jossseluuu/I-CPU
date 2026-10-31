# Biblioteca nucleo: todo el sistema salvo las interfaces de usuario.
# La CLI, la GUI y las pruebas enlazan contra ella.

add_library(icpu_core STATIC
    # core: utilidades basicas
    core/bits.cpp
    core/bytes.cpp
    core/error.cpp
    core/file.cpp
    core/json.cpp
    # crypto: Argon2id, XChaCha20-Poly1305, BLAKE2b, ChaCha20
    crypto/aead.cpp
    crypto/hash.cpp
    crypto/kdf.cpp
    crypto/keystream.cpp
    crypto/random.cpp
    # compress: DEFLATE
    compress/zlib.cpp
    # image: modelo raster y codecs espaciales
    image/codec.cpp
    image/format.cpp
    image/png.cpp
    image/pnm.cpp
    image/qoi.cpp
    image/raster.cpp
    # jpeg: coeficientes DCT (libjpeg-turbo)
    jpeg/jpeg.cpp
    # gif: codec GIF/LZW propio
    gif/gif.cpp
    # stego: portadoras, documentos y motor
    stego/dct_carrier.cpp
    stego/document.cpp
    stego/engine.cpp
    stego/palette_carrier.cpp
    stego/payload.cpp
    stego/pixel_carrier.cpp
    stego/slot_order.cpp
    # analysis: metricas y estegoanalisis
    analysis/metrics.cpp
    analysis/steganalysis.cpp
    analysis/visual.cpp
    # API de alto nivel
    icpu.cpp
    report.cpp
)

target_include_directories(icpu_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(icpu_core PRIVATE ICPU_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(icpu_core
    PUBLIC icpu_monocypher icpu_jpeg
    PRIVATE icpu_stb icpu_warnings)
if(WIN32)
    target_link_libraries(icpu_core PRIVATE bcrypt)
endif()
