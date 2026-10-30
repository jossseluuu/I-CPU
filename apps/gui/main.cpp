// icpu-gui: arranca el servidor local y abre la interfaz en el navegador.
//
//   icpu-gui [--port N] [--no-browser]

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "server.hpp"

#if defined(_WIN32)
#  include <windows.h>
#endif

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif
    icpu::gui::ServerConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-browser") {
            config.open_browser = false;
        } else if (a == "--port" && i + 1 < argc) {
            char* end = nullptr;
            const long p = std::strtol(argv[++i], &end, 10);
            if (*end != '\0' || p < 1 || p > 65535) {
                std::cerr << "Puerto invalido: " << argv[i] << "\n";
                return 2;
            }
            config.port = static_cast<int>(p);
        } else if (a == "-h" || a == "--help") {
            std::cout << "Uso: icpu-gui [--port N] [--no-browser]\n"
                         "  Abre la interfaz grafica de I-CPU en el navegador (servidor local en 127.0.0.1).\n";
            return 0;
        } else {
            std::cerr << "Opcion desconocida: " << a << " (usa --help)\n";
            return 2;
        }
    }
    try {
        return icpu::gui::run_server(config);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
