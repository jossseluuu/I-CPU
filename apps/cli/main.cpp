// Punto de entrada de la CLI: despacha el subcomando y traduce los errores
// a mensajes y codigos de salida.
//
// Codigos de salida:
//   0 = correcto          2 = argumentos       3 = E/S
//   4 = formato/datos     5 = capacidad        6 = autenticacion
//   70 = error interno

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "common.hpp"
#include "console.hpp"
#include "help.hpp"

namespace {

using icpu::ErrorCode;

int exit_code(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidArgument: return 2;
        case ErrorCode::Io: return 3;
        case ErrorCode::UnsupportedFormat:
        case ErrorCode::CorruptData: return 4;
        case ErrorCode::Capacity: return 5;
        case ErrorCode::Authentication: return 6;
        case ErrorCode::Internal: return 70;
    }
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    namespace cli = icpu::cli;
    std::vector<std::string> args = cli::init_console(argc, argv);

    // Sin argumentos: modo guiado si hay una persona delante; si no (tuberia, script), la ayuda.
    if (args.empty()) {
        if (!cli::stdin_is_terminal()) {
            cli::print_usage(std::cerr);
            return 2;
        }
        try {
            return cli::run_guided();
        } catch (const std::exception& e) {
            std::cerr << "Error inesperado: " << e.what() << "\n";
            return 70;
        }
    }
    if (args[0] == "help" || args[0] == "-h" || args[0] == "--help") {
        if (args.size() > 1 && !cli::print_command_help(std::cout, args[1])) {
            std::cerr << "Comando desconocido: " << args[1] << "\n";
            return 2;
        }
        if (args.size() == 1) cli::print_usage(std::cout);
        return 0;
    }
    const std::string command = args[0];
    args.erase(args.begin());
    for (const auto& a : args)
        if (a == "-h" || a == "--help") {
            if (cli::print_command_help(std::cout, command)) return 0;
            break;
        }

    try {
        if (command == "embed") return cli::cmd_embed(args);
        if (command == "extract") return cli::cmd_extract(args);
        if (command == "info") return cli::cmd_info(args);
        if (command == "analyze") return cli::cmd_analyze(args);
        if (command == "bench") return cli::cmd_bench(args);
        if (command == "formats") return cli::cmd_formats(args);
        if (command == "guided") return cli::run_guided();
        if (command == "version" || command == "--version") {
            std::cout << "icpu " << icpu::version() << "\n";
            return 0;
        }
        std::cerr << "Comando desconocido: " << command << " (usa icpu help)\n";
        return 2;
    } catch (const icpu::Error& e) {
        std::cerr << "Error (" << icpu::error_code_name(e.code()) << "): " << e.what() << "\n";
        return exit_code(e.code());
    } catch (const std::exception& e) {
        std::cerr << "Error inesperado: " << e.what() << "\n";
        return 70;
    }
}
