#pragma once
// Utilidades compartidas por los comandos de la CLI.

#include <filesystem>
#include <string>
#include <vector>

#include "args.hpp"
#include "icpu/core/json.hpp"
#include "icpu/image/raster.hpp"
#include "icpu/report.hpp"
#include "icpu/icpu.hpp"

namespace icpu::cli {

// Orden de prioridad: --password-file, --password, variable ICPU_PASSWORD, teclado.
std::string resolve_password(const Args& args, bool confirm);

Layout parse_layout(const std::string& s);
EmbedMode parse_mode(const std::string& s);
unsigned parse_bits(const std::string& s);
image::ImageFormat parse_format(const std::string& s);
int parse_int(const std::string& s, int lo, int hi, const char* what);

std::string human_bytes(std::uint64_t n);
std::string percent(double ratio);

int cmd_embed(const std::vector<std::string>& tokens);
int cmd_extract(const std::vector<std::string>& tokens);
int cmd_info(const std::vector<std::string>& tokens);
int cmd_analyze(const std::vector<std::string>& tokens);
int cmd_bench(const std::vector<std::string>& tokens);
int cmd_formats(const std::vector<std::string>& tokens);

}  // namespace icpu::cli
