#pragma once
// Jerarquia de errores de I-CPU. Todas las capas lanzan icpu::Error con un
// codigo que permite a las interfaces (CLI, GUI) traducirlo a un mensaje y a
// un codigo de salida sin depender del texto.

#include <stdexcept>
#include <string>

namespace icpu {

enum class ErrorCode {
    InvalidArgument,    // parametros incorrectos del usuario
    Io,                 // lectura/escritura de ficheros
    UnsupportedFormat,  // formato de imagen o variante no soportada
    CorruptData,        // fichero mal formado
    Capacity,           // el mensaje no cabe en la portadora
    Authentication,     // contrasena incorrecta, no hay mensaje o datos manipulados
    Internal            // fallo de invariante (bug)
};

class Error : public std::runtime_error {
public:
    Error(ErrorCode code, const std::string& message) : std::runtime_error(message), code_(code) {}
    ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

[[noreturn]] inline void fail(ErrorCode code, const std::string& message) { throw Error(code, message); }

inline void require(bool condition, ErrorCode code, const char* message) {
    if (!condition) fail(code, message);
}

const char* error_code_name(ErrorCode code) noexcept;

}  // namespace icpu
