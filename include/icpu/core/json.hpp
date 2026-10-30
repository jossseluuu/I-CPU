#pragma once
// Escritor JSON minimo (solo serializacion) para la salida --json de la CLI
// y para las respuestas de la API de la GUI.

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace icpu::json {

std::string escape(std::string_view s);

class Writer {
public:
    Writer& begin_object() { return open('{'); }
    Writer& end_object() { return close('}'); }
    Writer& begin_array() { return open('['); }
    Writer& end_array() { return close(']'); }

    Writer& key(std::string_view k) {
        comma();
        out_ += '"';
        out_ += escape(k);
        out_ += "\":";
        after_key_ = true;
        return *this;
    }
    Writer& value(std::string_view v) { return raw('"' + escape(v) + '"'); }
    Writer& value(const char* v) { return value(std::string_view(v)); }
    Writer& value(const std::string& v) { return value(std::string_view(v)); }
    Writer& value(bool v) { return raw(v ? "true" : "false"); }
    Writer& value(double v) { return raw(std::isfinite(v) ? std::to_string(v) : "null"); }
    Writer& value(std::uint64_t v) { return raw(std::to_string(v)); }
    Writer& value(std::int64_t v) { return raw(std::to_string(v)); }
    Writer& value(unsigned v) { return raw(std::to_string(v)); }
    Writer& value(int v) { return raw(std::to_string(v)); }
    Writer& null() { return raw("null"); }

    template <typename T>
    Writer& field(std::string_view k, const T& v) {
        key(k);
        return value(v);
    }

    const std::string& str() const { return out_; }

private:
    Writer& open(char c) {
        comma();
        out_ += c;
        first_ = true;
        return *this;
    }
    Writer& close(char c) {
        out_ += c;
        first_ = false;
        return *this;
    }
    Writer& raw(const std::string& s) {
        comma();
        out_ += s;
        return *this;
    }
    void comma() {
        if (after_key_) {
            after_key_ = false;
            return;
        }
        if (!first_) out_ += ',';
        first_ = false;
    }

    std::string out_;
    bool first_ = true;
    bool after_key_ = false;
};

}  // namespace icpu::json
