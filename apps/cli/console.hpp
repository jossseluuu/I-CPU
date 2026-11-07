#pragma once
// Integracion con la consola: argumentos y salida en UTF-8 (tambien en
// Windows) y lectura de contrasenas sin eco.

#include <optional>
#include <string>
#include <vector>

namespace icpu::cli {

// Configura la consola para UTF-8 y devuelve los argumentos en UTF-8.
std::vector<std::string> init_console(int argc, char** argv);

// Lee una linea sin mostrarla por pantalla (devuelve "" si la entrada no es una terminal).
std::string read_hidden(const std::string& prompt);

// Lee una linea visible en UTF-8 (en Windows via ReadConsoleW: std::cin no
// recibe bien caracteres no ASCII de la consola). nullopt = fin de la entrada.
std::optional<std::string> read_line(const std::string& prompt);

// Variable de entorno en UTF-8; nullopt si no existe o esta vacia.
std::optional<std::string> environment_utf8(const char* name);

// Forma canonica NFC: la misma contrasena debe dar los mismos bytes en cualquier
// sistema, porque "n con tilde" puede escribirse con uno o con dos puntos de codigo.
std::string normalize_password(const std::string& utf8);

bool stdin_is_terminal();
bool stdout_is_terminal();
std::string read_all_stdin();

}  // namespace icpu::cli
