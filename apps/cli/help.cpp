#include "help.hpp"

#include "icpu/icpu.hpp"

namespace icpu::cli {

namespace {

struct CommandHelp {
    const char* name;
    const char* text;
};

constexpr CommandHelp kCommands[] = {
    {"embed",
     "icpu embed <portadora> -o <salida> (-m TEXTO | -f FICHERO | --stdin) [opciones]\n"
     "\n"
     "  Oculta un texto o un fichero dentro de una imagen, cifrado con la contrasena.\n"
     "  El formato de salida lo decide la extension de -o (o --format):\n"
     "    .png .bmp .tga .qoi .ppm  dominio espacial (sin perdida, la mayor capacidad)\n"
     "    .jpg                      coeficientes DCT (si la portadora no es JPEG, se comprime antes)\n"
     "    .gif                      paleta (solo con portadora GIF)\n"
     "\n"
     "  Que ocultar (exactamente uno):\n"
     "    -m, --message TEXTO     un texto\n"
     "    -f, --file FICHERO      un fichero (se recupera con su nombre original)\n"
     "    --stdin                 lo que llegue por la entrada estandar\n"
     "\n"
     "  Opciones:\n"
     "    -o, --output FICHERO    imagen resultante (obligatorio)\n"
     "    --format EXT            fuerza el formato de salida\n"
     "    --mode adaptive|match|replace\n"
     "                            adaptive (por defecto): STC, carga limitada, lo menos detectable\n"
     "                            match: LSB matching +-1; replace: sustitucion LSB (la mas detectable)\n"
     "    --bits auto|1..4        bits por ranura; solo con replace. auto = el minimo con el que cabe\n"
     "    --layout random|sequential  orden de escritura; sequential solo para experimentos\n"
     "    --quality 1..100        calidad si hay que convertir a JPEG (por defecto 90)\n"
     "    --no-compress           no comprimir el mensaje antes de cifrarlo\n"
     "    --no-verify             no releer la imagen para comprobar que el mensaje se recupera\n"
     "    --force                 sobrescribir la salida si existe\n"
     "    --json                  resultado en JSON\n"
     "\n"
     "  Ejemplos:\n"
     "    icpu embed foto.png -o secreta.png -m \"Nos vemos a las 8\"\n"
     "    icpu embed foto.jpg -o secreta.jpg -f contrato.pdf\n"
     "    icpu embed foto.png -o secreta.png -m \"hola\" --mode match\n"},
    {"extract",
     "icpu extract <imagen> [-o SALIDA] [opciones]\n"
     "\n"
     "  Recupera el mensaje oculto. Si es un texto, lo muestra; si es un fichero, lo\n"
     "  guarda con su nombre original en la carpeta actual (o donde indique -o).\n"
     "\n"
     "  Opciones:\n"
     "    -o, --output RUTA       fichero o carpeta donde guardar lo recuperado\n"
     "    --layout random|sequential  el orden usado al ocultar (por defecto random)\n"
     "    --force                 sobrescribir si el fichero ya existe\n"
     "    --json                  resultado en JSON\n"
     "\n"
     "  Si la contrasena es incorrecta, la imagen no tiene mensaje o se ha modificado,\n"
     "  el error es el mismo a proposito (codigo de salida 6).\n"
     "\n"
     "  Ejemplos:\n"
     "    icpu extract secreta.png\n"
     "    icpu extract secreta.jpg -o C:\\recuperados\\\n"},
    {"info",
     "icpu info <imagen> [--json]\n"
     "\n"
     "  Muestra formato, dimensiones y cuanto se puede ocultar en cada formato de salida\n"
     "  y con cada numero de bits por ranura.\n"
     "\n"
     "  Ejemplo:\n"
     "    icpu info foto.png\n"},
    {"analyze",
     "icpu analyze <imagen> [--cover ORIGINAL] [--export DIR] [--steps N] [--json]\n"
     "\n"
     "  Estegoanalisis: chi-cuadrado y su barrido (acumulado y por ventanas), analisis RS\n"
     "  (imagenes sin perdida) o chi-cuadrado DCT (JPEG). Con --cover compara con la\n"
     "  original (MSE, PSNR, SSIM).\n"
     "\n"
     "  Opciones:\n"
     "    --cover ORIGINAL        la portadora sin mensaje, para medir la distorsion\n"
     "    --export DIR            guarda planos de bits, mapa de diferencias e histogramas\n"
     "    --steps N               porciones del barrido chi-cuadrado (por defecto 20)\n"
     "    --json                  resultado en JSON\n"
     "\n"
     "  Ejemplo:\n"
     "    icpu analyze secreta.png --cover foto.png --export analisis\n"},
    {"bench",
     "icpu bench <portadora> [opciones]\n"
     "\n"
     "  Experimento: oculta mensajes aleatorios variando los parametros y mide la\n"
     "  distorsion (PSNR, SSIM) y lo detectable que es cada combinacion.\n"
     "\n"
     "  Opciones (listas separadas por comas):\n"
     "    --fill LISTA            fracciones de la capacidad (por defecto 0.05,0.1,0.25,0.5,1)\n"
     "    --bits LISTA            bits por ranura (por defecto 1,2)\n"
     "    --modes LISTA           adaptive,replace,match (por defecto los tres)\n"
     "    --layouts LISTA         random,sequential (por defecto ambos)\n"
     "    --format EXT            formato de salida\n"
     "    --csv FICHERO           guarda los resultados en CSV\n"
     "    --json                  resultado en JSON\n"
     "\n"
     "  Ejemplo:\n"
     "    icpu bench foto.png --bits 1,2,3 --csv resultados.csv\n"},
    {"guided",
     "icpu guided          (o simplemente: icpu, sin argumentos)\n"
     "\n"
     "  Modo guiado: un menu que pregunta paso a paso que hacer (ocultar, extraer o ver la\n"
     "  capacidad de una imagen). Puedes arrastrar las imagenes a la consola para escribir\n"
     "  su ruta. Usa siempre el modo adaptativo y pide la contrasena sin mostrarla.\n"},
    {"formats", "icpu formats\n\n  Tabla de formatos soportados.\n"},
    {"version", "icpu version\n\n  Version del programa.\n"},
};

}  // namespace

void print_usage(std::ostream& out) {
    out << "I-CPU " << version() << " - esteganografia cifrada en imagenes (espacial, DCT y paleta)\n"
        << "\n"
           "Uso: icpu <comando> [opciones]        (sin argumentos: modo guiado paso a paso)\n"
           "\n"
           "Comandos:\n"
           "  embed      Oculta un texto o un fichero en una imagen\n"
           "  extract    Recupera el mensaje oculto\n"
           "  info       Formato de la imagen y capacidad en cada dominio\n"
           "  analyze    Estegoanalisis y metricas de distorsion\n"
           "  bench      Experimento sistematico de distorsion y detectabilidad\n"
           "  guided     Modo guiado paso a paso (lo mismo que ejecutar icpu sin nada)\n"
           "  formats    Formatos soportados\n"
           "  version    Version del programa\n"
           "\n"
           "  icpu help <comando>  (o icpu <comando> --help) muestra todas sus opciones.\n"
           "\n"
           "Ejemplos:\n"
           "  icpu embed foto.png -o secreta.png -m \"mensaje\"   oculta un texto (pide la contrasena)\n"
           "  icpu embed foto.jpg -o secreta.jpg -f doc.pdf     oculta un fichero en un JPEG\n"
           "  icpu extract secreta.png                          recupera el mensaje\n"
           "  icpu info foto.png                                cuanto cabe en la imagen\n"
           "\n"
           "Contrasena (embed/extract): --password-file F, -p CLAVE, variable ICPU_PASSWORD o teclado.\n"
           "Importante: no envies la imagen por WhatsApp o redes sociales (la recomprimen y el\n"
           "mensaje se pierde); enviala como archivo.\n";
}

bool print_command_help(std::ostream& out, const std::string& command) {
    for (const auto& c : kCommands)
        if (command == c.name) {
            out << "Uso: " << c.text;
            return true;
        }
    return false;
}

}  // namespace icpu::cli
