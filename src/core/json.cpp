#include "icpu/core/json.hpp"

#include <cstdio>

namespace icpu::json {

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += ch;  // UTF-8 se copia tal cual
                }
        }
    }
    return out;
}

}  // namespace icpu::json
