#pragma once
// Textos de ayuda de la CLI: general (con ejemplos) y detallada por comando.

#include <ostream>
#include <string>

namespace icpu::cli {

void print_usage(std::ostream& out);

// Devuelve false si el comando no existe.
bool print_command_help(std::ostream& out, const std::string& command);

// Modo guiado: pregunta paso a paso que hacer (se usa al ejecutar icpu sin argumentos).
int run_guided();

}  // namespace icpu::cli
