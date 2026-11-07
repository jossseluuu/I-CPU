cmake_minimum_required(VERSION 3.20)

project(icpu
    VERSION 2.0.0
    DESCRIPTION "Esteganografia cifrada multi-dominio (espacial, DCT y paleta)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de compilacion" FORCE)
endif()

option(ICPU_BUILD_CLI   "Compilar la interfaz de linea de comandos" ON)
option(ICPU_BUILD_GUI   "Compilar la interfaz grafica (servidor web local)" ON)
option(ICPU_BUILD_TESTS "Compilar las pruebas unitarias" ON)
option(ICPU_BUILD_FUZZ  "Compilar los objetivos de fuzzing (requiere Clang)" OFF)
option(ICPU_SANITIZE    "Compilar con AddressSanitizer y UBSan" OFF)
option(ICPU_STATIC_RUNTIME "Enlazar el runtime de C++ de forma estatica (MinGW)" ON)

if(ICPU_SANITIZE)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Opciones de aviso comunes solo para el codigo propio (no para third_party).
add_library(icpu_warnings INTERFACE)
if(MSVC)
    target_compile_options(icpu_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(icpu_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# Runtime de C/C++ enlazado de forma estatica: los .exe funcionan en cualquier
# Windows sin instalar redistribuibles.
if(ICPU_STATIC_RUNTIME)
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
    elseif(MINGW)
        add_link_options(-static -static-libgcc -static-libstdc++)
    endif()
endif()
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    # Endurecimiento: estos programas procesan imagenes de origen desconocido.
    if(NOT ICPU_SANITIZE)
        add_compile_options(/guard:cf)
        add_link_options(/guard:cf /DYNAMICBASE /NXCOMPAT)
    endif()
endif()

add_subdirectory(third_party)
add_subdirectory(src)

if(ICPU_BUILD_CLI)
    add_subdirectory(apps/cli)
endif()
if(ICPU_BUILD_GUI)
    add_subdirectory(apps/gui)
endif()
if(ICPU_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(ICPU_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()
