# Convierte los ficheros del frontend (web/) en un .cpp con arrays de bytes,
# para que icpu-gui sea un unico ejecutable sin carpetas adicionales.
#
# Uso: cmake -DROOT=<dir web> -DFILES=a.html,b.js -DOUT=<salida.cpp> -P embed_assets.cmake

string(REPLACE "," ";" files "${FILES}")

set(body "")
set(table "")
set(i 0)
foreach(name IN LISTS files)
    file(READ "${ROOT}/${name}" hex HEX)
    string(LENGTH "${hex}" hex_len)
    if(hex_len EQUAL 0)
        message(FATAL_ERROR "Recurso vacio: ${name}")
    endif()
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")

    get_filename_component(ext "${name}" LAST_EXT)
    if(ext STREQUAL ".html")
        set(mime "text/html; charset=utf-8")
    elseif(ext STREQUAL ".css")
        set(mime "text/css; charset=utf-8")
    elseif(ext STREQUAL ".js")
        set(mime "text/javascript; charset=utf-8")
    elseif(ext STREQUAL ".svg")
        set(mime "image/svg+xml")
    else()
        set(mime "application/octet-stream")
    endif()

    string(APPEND body "const unsigned char asset_${i}[] = {${bytes}};\n")
    string(APPEND table "    {\"/${name}\", \"${mime}\", asset_${i}, sizeof(asset_${i})},\n")
    math(EXPR i "${i} + 1")
endforeach()

set(content "// Generado por embed_assets.cmake: no editar.\n#include \"assets.hpp\"\n\nnamespace icpu::gui {\n\nnamespace {\n${body}}  // namespace\n\nconst Asset kAssets[] = {\n${table}};\nconst std::size_t kAssetCount = ${i};\n\n}  // namespace icpu::gui\n")

# Solo se reescribe si cambia, para no recompilar sin necesidad.
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
    if(old STREQUAL content)
        return()
    endif()
endif()
file(WRITE "${OUT}" "${content}")
