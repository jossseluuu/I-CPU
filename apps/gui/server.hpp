#pragma once
// Servidor HTTP local de la GUI.
//
// Seguridad (el servidor maneja contrasenas y ficheros privados):
//   - Escucha solo en 127.0.0.1: no es accesible desde la red.
//   - Toda llamada a /api/ exige la cabecera X-ICPU-Token con un token
//     aleatorio de 128 bits generado al arrancar. El navegador lo recibe en
//     el fragmento de la URL (#t=...), que nunca se envia al servidor ni queda
//     en registros. Una web maliciosa no puede anadir esa cabecera a una
//     peticion a otro origen sin una verificacion CORS que aqui no se concede.
//   - Se rechaza cualquier cabecera Host distinta de 127.0.0.1/localhost:puerto
//     (proteccion frente a "DNS rebinding").

#include <string>

namespace httplib {
class Server;
}

namespace icpu::gui {

struct ServerConfig {
    int port = 0;  // 0 = el sistema elige un puerto libre
    bool open_browser = true;
};

// Bloquea hasta que se cierra el servidor (Ctrl+C o boton "Cerrar" de la web).
int run_server(const ServerConfig& config);

// Piezas sueltas (las usan run_server y las pruebas):
// token aleatorio de 128 bits en hexadecimal...
std::string new_session_token();
// ...y registro de rutas y comprobaciones de seguridad. `token` y `port` se
// leen en cada peticion (el puerto solo se conoce tras el bind), asi que deben
// vivir mientras viva el servidor.
void configure(httplib::Server& svr, const std::string& token, const int& port);

}  // namespace icpu::gui
