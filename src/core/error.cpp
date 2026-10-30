#include "icpu/core/error.hpp"

namespace icpu {

const char* error_code_name(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidArgument: return "argumento-invalido";
        case ErrorCode::Io: return "entrada-salida";
        case ErrorCode::UnsupportedFormat: return "formato-no-soportado";
        case ErrorCode::CorruptData: return "datos-corruptos";
        case ErrorCode::Capacity: return "capacidad-insuficiente";
        case ErrorCode::Authentication: return "autenticacion";
        case ErrorCode::Internal: return "interno";
    }
    return "desconocido";
}

}  // namespace icpu
