# Dependencias de terceros. Todas se distribuyen con el repositorio (vendored)
# y sus versiones exactas estan en VERSIONS.txt.

# stb_image / stb_image_write (dominio publico / MIT): lectura de PNG, BMP, TGA, PSD, HDR...
add_library(icpu_stb STATIC stb/stb_impl.c)
target_include_directories(icpu_stb SYSTEM PUBLIC stb)

# Monocypher 4 (BSD-2 / CC0): Argon2id, XChaCha20-Poly1305, BLAKE2b, ChaCha20
add_library(icpu_monocypher STATIC monocypher/monocypher.c)
target_include_directories(icpu_monocypher SYSTEM PUBLIC monocypher)

# cpp-httplib (MIT): servidor HTTP local para la GUI
add_library(icpu_httplib INTERFACE)
target_include_directories(icpu_httplib SYSTEM INTERFACE httplib)
find_package(Threads REQUIRED)  # httplib atiende peticiones en un pool de hilos (-pthread en Linux)
target_link_libraries(icpu_httplib INTERFACE Threads::Threads)
if(WIN32)
    target_link_libraries(icpu_httplib INTERFACE ws2_32 crypt32)
endif()

# doctest (MIT): framework de pruebas
add_library(icpu_doctest INTERFACE)
target_include_directories(icpu_doctest SYSTEM INTERFACE doctest)

# libjpeg-turbo (BSD-3 / IJG): acceso a coeficientes DCT de JPEG.
# Se compila como proyecto externo con su propio CMake (sin SIMD para no
# depender de NASM) y se instala dentro del directorio de compilacion.
include(ExternalProject)
set(ICPU_JPEG_PREFIX ${CMAKE_BINARY_DIR}/deps/libjpeg-turbo)
if(MSVC)
    set(ICPU_JPEG_LIB ${ICPU_JPEG_PREFIX}/lib/jpeg-static.lib)
else()
    set(ICPU_JPEG_LIB ${ICPU_JPEG_PREFIX}/lib/libjpeg.a)
endif()
ExternalProject_Add(libjpeg_turbo_build
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libjpeg-turbo
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_INSTALL_PREFIX=${ICPU_JPEG_PREFIX}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
        -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        -DWITH_SIMD=OFF
        -DWITH_TURBOJPEG=OFF
        -DWITH_TOOLS=OFF
        -DWITH_TESTS=OFF
    BUILD_BYPRODUCTS ${ICPU_JPEG_LIB}
    UPDATE_COMMAND ""
)
file(MAKE_DIRECTORY ${ICPU_JPEG_PREFIX}/include)
add_library(icpu_jpeg STATIC IMPORTED GLOBAL)
set_target_properties(icpu_jpeg PROPERTIES
    IMPORTED_LOCATION ${ICPU_JPEG_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${ICPU_JPEG_PREFIX}/include)
add_dependencies(icpu_jpeg libjpeg_turbo_build)
