#pragma once
// Ficheros del frontend embebidos en el ejecutable (generados en tiempo de
// compilacion a partir de apps/gui/web por embed_assets.cmake).

#include <cstddef>
#include <string_view>

namespace icpu::gui {

struct Asset {
    const char* path;  // "/index.html"
    const char* mime;
    const unsigned char* data;
    std::size_t size;
};

extern const Asset kAssets[];
extern const std::size_t kAssetCount;

inline const Asset* find_asset(std::string_view path) {
    for (std::size_t i = 0; i < kAssetCount; ++i)
        if (path == kAssets[i].path) return &kAssets[i];
    return nullptr;
}

}  // namespace icpu::gui
