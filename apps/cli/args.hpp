#pragma once
// Analizador de argumentos minimo: posicionales, opciones con valor
// ("-o salida.png", "--bits 2", "--bits=2") y banderas ("--json").

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace icpu::cli {

class Args {
public:
    // `flags`: nombres de las opciones booleanas (sin valor) que acepta el comando.
    Args(const std::vector<std::string>& tokens, std::set<std::string> flags);

    std::optional<std::string> value(std::initializer_list<const char*> names) const;
    std::string value_or(std::initializer_list<const char*> names, std::string fallback) const;
    bool flag(std::initializer_list<const char*> names) const;
    const std::vector<std::string>& positionals() const { return positionals_; }

    // Falla si se uso alguna opcion que el comando no consulto (erratas del usuario).
    void reject_unknown() const;

private:
    std::vector<std::pair<std::string, std::string>> options_;
    std::set<std::string> present_flags_;
    std::set<std::string> missing_value_;  // opciones al final de la linea, sin valor
    std::set<std::string> flags_;
    std::vector<std::string> positionals_;
    mutable std::set<std::string> consulted_;
};

}  // namespace icpu::cli
