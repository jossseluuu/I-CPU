// Modo guiado: al ejecutar "icpu" sin argumentos en una terminal (o con doble
// clic desde el Explorador) se pregunta paso a paso que hacer. Por debajo
// llama a los mismos comandos que la linea de comandos.

#include <filesystem>
#include <iostream>

#include "common.hpp"
#include "console.hpp"
#include "help.hpp"
#include "icpu/core/file.hpp"

namespace icpu::cli {

namespace {

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    s = s.substr(first, s.find_last_not_of(" \t") - first + 1);
    return s;
}

// Al arrastrar un fichero a la consola llega entre comillas (y en PowerShell, a veces con "& ").
std::string clean_path(std::string s) {
    s = trim(std::move(s));
    if (s.rfind("& ", 0) == 0) s = trim(s.substr(2));
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) s = s.substr(1, s.size() - 2);
    return s;
}

// nullopt = el usuario cerro la entrada (Ctrl+Z / Ctrl+D)
std::optional<std::string> ask(const std::string& prompt, const std::string& fallback = {}) {
    auto line = read_line(prompt);
    if (!line) return std::nullopt;
    std::string v = trim(*line);
    return v.empty() ? fallback : v;
}

std::optional<std::string> ask_existing_file(const std::string& prompt) {
    for (;;) {
        auto v = ask(prompt);
        if (!v) return std::nullopt;
        const std::string path = clean_path(*v);
        if (path.empty()) continue;
        if (std::filesystem::is_regular_file(utf8_path(path))) return path;
        std::cout << "  No existe el fichero: " << path << "\n";
    }
}

bool ask_yes(const std::string& prompt) {
    const auto v = ask(prompt + " [s/N]: ");
    return v && (*v == "s" || *v == "S" || *v == "si" || *v == "y");
}

void show_capacity(const std::string& path) {
    const ImageInfo info = inspect(read_file(utf8_path(path)), path_utf8(utf8_path(path).filename()));
    std::cout << "  " << image::format_name(info.format) << ", " << info.width << " x " << info.height
              << " px. Capacidad aproximada (1 bit por ranura):\n";
    for (const auto& c : info.capacities)
        if (c.bits_per_slot == 1)
            std::cout << "    - salida " << image::format_name(c.output) << " (" << image::domain_name(c.domain)
                      << "): hasta " << human_bytes(c.message_bytes) << "\n";
}

int guided_embed() {
    const auto cover = ask_existing_file("Imagen portadora (puedes arrastrarla aqui): ");
    if (!cover) return 0;
    show_capacity(*cover);

    std::vector<std::string> tokens{*cover};
    const auto kind = ask("Ocultar (1) un texto o (2) un fichero? [1]: ", "1");
    if (!kind) return 0;
    if (*kind == "2") {
        const auto file = ask_existing_file("Fichero a ocultar: ");
        if (!file) return 0;
        tokens.insert(tokens.end(), {"-f", *file});
    } else {
        const auto msg = ask("Mensaje: ");
        if (!msg) return 0;
        if (msg->empty()) {
            std::cout << "  El mensaje esta vacio.\n";
            return 0;
        }
        tokens.insert(tokens.end(), {"-m", *msg});
    }

    // Salida por defecto: junto a la portadora, "<nombre>_icpu.<ext>" (PNG si el formato no se puede escribir).
    const std::filesystem::path cover_path = utf8_path(*cover);
    const auto source = image::format_from_extension(lower_extension(cover_path));
    const std::string ext = image::can_write(source) ? image::default_extension(source) : "png";
    const std::string suggested =
        path_utf8(cover_path.parent_path() / utf8_path(path_utf8(cover_path.stem()) + "_icpu." + ext));
    const auto out = ask("Imagen de salida [" + suggested + "]: ", suggested);
    if (!out) return 0;
    const std::string out_path = clean_path(*out);
    tokens.insert(tokens.end(), {"-o", out_path});
    if (std::filesystem::exists(utf8_path(out_path))) {
        if (!ask_yes("  Ya existe. Sobrescribir?")) return 0;
        tokens.push_back("--force");
    }
    std::cout << "\n";
    return cmd_embed(tokens);  // pide la contrasena dos veces, sin mostrarla
}

int guided_extract() {
    const auto image = ask_existing_file("Imagen con el mensaje (puedes arrastrarla aqui): ");
    if (!image) return 0;
    std::cout << "  (si es un fichero, se guardara en " << path_utf8(std::filesystem::current_path()) << ")\n\n";
    return cmd_extract({*image});
}

int guided_info() {
    const auto image = ask_existing_file("Imagen: ");
    if (!image) return 0;
    std::cout << "\n";
    return cmd_info({*image});
}

}  // namespace

int run_guided() {
    std::cout << "I-CPU " << version() << " - modo guiado\n"
              << "(para usarlo con argumentos: icpu help)\n";
    for (;;) {
        std::cout << "\nQue quieres hacer?\n"
                     "  1) Ocultar un mensaje o un fichero en una imagen\n"
                     "  2) Extraer un mensaje\n"
                     "  3) Ver informacion y capacidad de una imagen\n"
                     "  4) Salir\n";
        const auto choice = ask("> ");
        if (!choice || *choice == "4" || *choice == "q" || *choice == "salir") return 0;
        std::cout << "\n";
        try {
            if (*choice == "1") guided_embed();
            else if (*choice == "2") guided_extract();
            else if (*choice == "3") guided_info();
            else std::cout << "Opcion no valida.\n";
        } catch (const Error& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }
}

}  // namespace icpu::cli
