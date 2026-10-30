#pragma once
// Integracion con la consola: argumentos y salida en UTF-8 (tambien en
// Windows) y lectura de contrasenas sin eco.

#include <string>
#include <vector>

namespace icpu::cli {

// Configura la consola para UTF-8 y devuelve los argumentos en UTF-8.
std::vector<std::string> init_console(int argc, char** argv);

// Lee una linea sin mostrarla por pantalla (devuelve "" si la entrada no es una terminal).
std::string read_hidden(const std::string& prompt);

bool stdin_is_terminal();
std::string read_all_stdin();

}  // namespace icpu::cli
