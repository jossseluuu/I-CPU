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

//
// Ciclo de vida (ver run_server):
//   - El proceso termina con el boton de apagado de la web, con Ctrl+C, al
//     cerrar la consola o cuando se cierran todas las pestanas de la web.
//   - Si queda una operacion en curso (p. ej. un experimento), no se espera:
//     el proceso termina y la operacion se cancela.
//   - En Windows solo hay una instancia: si ya hay una abierta, la nueva le
//     pide que abra otra pestana del navegador y termina.

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace httplib {
class Server;
}

namespace icpu::gui {

struct ServerConfig {
    int port = 0;  // 0 = el sistema elige un puerto libre
    bool open_browser = true;
};

// Bloquea hasta que se cierra el servidor.
int run_server(const ServerConfig& config);

// Pestanas de la web abiertas. Cada pestana se presenta al cargar ("hello"),
// avisa cada pocos segundos de que sigue viva ("ping") y se despide al
// cerrarse ("bye"). Si el navegador se cierra de golpe no hay despedida, y la
// pestana se da por cerrada cuando deja de avisar durante `stale`.
// El reloj se pasa como argumento para poder probarla sin esperar.
class TabTracker {
public:
    using Clock = std::chrono::steady_clock;

    void seen(const std::string& id, Clock::time_point now);  // hello y ping
    void bye(const std::string& id, Clock::time_point now);

    // Verdadero si alguna vez hubo pestanas y ya no queda ninguna desde hace
    // al menos `grace` (el margen evita cerrar al recargar la pagina).
    bool abandoned(Clock::time_point now, Clock::duration grace, Clock::duration stale);
    std::size_t open_tabs(Clock::time_point now, Clock::duration stale);

    static constexpr std::size_t kMaxTabs = 64;

private:
    void prune(Clock::time_point now, Clock::duration stale);  // con el mutex tomado

    std::mutex mutex_;
    std::map<std::string, Clock::time_point> tabs_;
    bool ever_ = false;
    Clock::time_point empty_since_{};
};

// Lo que run_server comparte con las rutas: las pestanas abiertas y como
// pedir el cierre. Sin Lifecycle (pruebas), el apagado solo detiene el servidor.
struct Lifecycle {
    TabTracker tabs;
    std::function<void()> request_shutdown;
};

// Piezas sueltas (las usan run_server y las pruebas):
// token aleatorio de 128 bits en hexadecimal...
std::string new_session_token();
// ...y registro de rutas y comprobaciones de seguridad. `token`, `port` y
// `life` se leen en cada peticion (el puerto solo se conoce tras el bind), asi
// que deben vivir mientras viva el servidor.
void configure(httplib::Server& svr, const std::string& token, const int& port, Lifecycle* life = nullptr);

}  // namespace icpu::gui
