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

namespace {

using icpu::ErrorCode;

constexpr const char* kUsage =
    "I-CPU - esteganografia cifrada en imagenes (espacial, DCT y paleta)\n"
    "\n"
    "Uso: icpu <comando> [opciones]\n"
    "\n"
    "Comandos:\n"
    "  embed <portadora> -o <salida> (-m TEXTO | -f FICHERO | --stdin)\n"
    "        Oculta un texto o un fichero.\n"
    "        --bits auto|1..4     bits por ranura (auto = el minimo que permita que quepa)\n"
    "        --mode replace|match sustitucion LSB o LSB matching (+-1)\n"
    "        --layout random|sequential\n"
    "        --format EXT         formato de salida (por defecto, el de -o o la portadora)\n"
    "        --quality 1..100     calidad si hay que convertir a JPEG (por defecto 90)\n"
    "        --no-compress  --no-verify  --force  --json\n"
    "\n"
    "  extract <imagen> [-o SALIDA] [--layout random|sequential] [--force] [--json]\n"
    "        Recupera el mensaje oculto.\n"
    "\n"
    "  info <imagen> [--json]\n"
    "        Caracteristicas de la imagen y capacidad en cada dominio.\n"
    "\n"
    "  analyze <imagen> [--cover ORIGINAL] [--export DIR] [--steps N] [--json]\n"
    "        Estegoanalisis (chi-cuadrado, RS, chi-cuadrado DCT) y, con --cover,\n"
    "        metricas de distorsion (MSE, PSNR, SSIM).\n"
    "\n"
    "  bench <portadora> [--fill LISTA] [--bits LISTA] [--modes LISTA] [--layouts LISTA]\n"
    "        [--format EXT] [--csv FICHERO] [--json]\n"
    "        Experimento: oculta mensajes aleatorios variando los parametros y mide\n"
    "        distorsion y detectabilidad. --fill son fracciones de la capacidad\n"
    "        (por defecto 0.05,0.1,0.25,0.5,1).\n"
    "\n"
    "  formats             Tabla de formatos soportados.\n"
    "  version             Version del programa.\n"
    "\n"
    "Contrasena (embed/extract): --password-file F, -p CLAVE, variable ICPU_PASSWORD o teclado.\n";

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
    if (args.empty() || args[0] == "help" || args[0] == "-h" || args[0] == "--help") {
        std::cout << kUsage;
        return args.empty() ? 2 : 0;
    }
    const std::string command = args[0];
    args.erase(args.begin());

    try {
        if (command == "embed") return cli::cmd_embed(args);
        if (command == "extract") return cli::cmd_extract(args);
        if (command == "info") return cli::cmd_info(args);
        if (command == "analyze") return cli::cmd_analyze(args);
        if (command == "bench") return cli::cmd_bench(args);
        if (command == "formats") return cli::cmd_formats(args);
        if (command == "version" || command == "--version") {
            std::cout << "icpu " << icpu::version() << "\n";
            return 0;
        }
        std::cerr << "Comando desconocido: " << command << "\n\n" << kUsage;
        return 2;
    } catch (const icpu::Error& e) {
        std::cerr << "Error (" << icpu::error_code_name(e.code()) << "): " << e.what() << "\n";
        return exit_code(e.code());
    } catch (const std::exception& e) {
        std::cerr << "Error inesperado: " << e.what() << "\n";
        return 70;
    }
}
