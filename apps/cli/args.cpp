#include "args.hpp"

#include "icpu/core/error.hpp"

namespace icpu::cli {

Args::Args(const std::vector<std::string>& tokens, std::set<std::string> flags) : flags_(std::move(flags)) {
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        if (t.size() < 2 || t[0] != '-' || t == "--") {
            positionals_.push_back(t);
            continue;
        }
        std::string name = t, inline_value;
        bool has_inline = false;
        if (const auto eq = t.find('='); eq != std::string::npos && t.rfind("--", 0) == 0) {
            name = t.substr(0, eq);
            inline_value = t.substr(eq + 1);
            has_inline = true;
        }
        if (flags_.count(name)) {
            present_flags_.insert(name);
        } else if (has_inline) {
            options_.emplace_back(name, inline_value);
        } else if (i + 1 >= tokens.size()) {
            // Se decide despues si es una opcion sin valor o una desconocida (errata).
            missing_value_.insert(name);
        } else {
            options_.emplace_back(name, tokens[++i]);
        }
    }
}

std::optional<std::string> Args::value(std::initializer_list<const char*> names) const {
    for (const char* n : names) {
        consulted_.insert(n);
        if (missing_value_.count(n)) fail(ErrorCode::InvalidArgument, std::string("La opcion ") + n + " necesita un valor");
        for (auto it = options_.rbegin(); it != options_.rend(); ++it)
            if (it->first == n) return it->second;
    }
    return std::nullopt;
}

std::string Args::value_or(std::initializer_list<const char*> names, std::string fallback) const {
    auto v = value(names);
    return v ? *v : std::move(fallback);
}

bool Args::flag(std::initializer_list<const char*> names) const {
    for (const char* n : names) {
        consulted_.insert(n);
        if (present_flags_.count(n)) return true;
    }
    return false;
}

void Args::reject_unknown() const {
    for (const auto& [name, v] : options_)
        if (!consulted_.count(name)) fail(ErrorCode::InvalidArgument, "Opcion desconocida: " + name);
    for (const auto& name : missing_value_)
        if (!consulted_.count(name)) fail(ErrorCode::InvalidArgument, "Opcion desconocida: " + name);
}

}  // namespace icpu::cli
