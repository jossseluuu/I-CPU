#include "icpu/stego/stc.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>

#include "icpu/core/error.hpp"

namespace icpu::stego::stc {

namespace {

constexpr std::size_t kStates = std::size_t(1) << kHeight;
constexpr std::size_t kPathBytes = kStates / 8;
constexpr std::size_t kMaxSegment = std::size_t(1) << 20;  // 32 MiB de memoria por segmento
constexpr double kInfinity = std::numeric_limits<double>::infinity();

// Columnas de la submatriz. Son parte del formato: no pueden cambiar. El primer
// y el ultimo bit a 1 garantizan que cada columna afecta a todo el bloque.
const std::array<std::uint8_t, kMaxWidth + 1>& columns() {
    static const std::array<std::uint8_t, kMaxWidth + 1> table = [] {
        std::array<std::uint8_t, kMaxWidth + 1> t{};
        std::uint64_t state = 0x1C9A3F5B7E2D4806ull;
        for (auto& column : t) {
            std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            column = static_cast<std::uint8_t>((z >> 40) | 0x81u);
        }
        return t;
    }();
    return table;
}

struct Segment {
    std::size_t first_element, elements, first_bit, bits;
};

std::vector<Segment> plan(std::size_t n, std::size_t m) {
    const std::size_t count = std::max<std::size_t>(1, (n + kMaxSegment - 1) / kMaxSegment);
    std::vector<Segment> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t e0 = n / count * i + (n % count) * i / count;
        const std::size_t e1 = n / count * (i + 1) + (n % count) * (i + 1) / count;
        const std::size_t b0 = m / count * i + (m % count) * i / count;
        const std::size_t b1 = m / count * (i + 1) + (m % count) * (i + 1) / count;
        out[i] = {e0, e1 - e0, b0, b1 - b0};
    }
    return out;
}

// Elementos que pertenecen al bloque i (anchos w y w + 1 para repartir n exactamente).
std::size_t block_start(std::size_t i, std::size_t n, std::size_t m) {
    return n / m * i + (n % m) * i / m;
}

std::uint8_t block_mask(std::size_t i, std::size_t m) {
    const std::size_t rows = std::min<std::size_t>(kHeight, m - i);
    return static_cast<std::uint8_t>((std::size_t(1) << rows) - 1);
}

bool embed_segment(const std::uint8_t* lsb, const float* costs, const std::uint8_t* message, const Segment& seg,
                   std::uint8_t* out) {
    const std::size_t n = seg.elements, m = seg.bits;
    const auto& table = columns();
    std::vector<std::uint8_t> path(n * kPathBytes, 0);
    std::array<double, kStates> cost, next;
    cost.fill(kInfinity);
    cost[0] = 0;

    // Avance: cost[s] es el menor coste con el que se llega al sindrome parcial s.
    for (std::size_t i = 0, e = 0; i < m; ++i) {
        const std::uint8_t mask = block_mask(i, m);
        const std::size_t end = block_start(i + 1, n, m);
        for (std::size_t j = 0; e < end; ++e, ++j) {
            const std::uint8_t column = table[j] & mask;
            const std::uint8_t x = lsb[e];
            const float rho = costs[e];
            std::uint8_t* bits = &path[e * kPathBytes];
            if (std::isinf(rho)) {  // el elemento no puede cambiar: solo hay un camino
                if (x == 0) continue;
                for (std::size_t s = 0; s < kStates; ++s) next[s] = cost[s ^ column];
                std::fill_n(bits, kPathBytes, std::uint8_t(0xFF));
                cost = next;
                continue;
            }
            const double keep0 = x == 0 ? 0.0 : double(rho);
            const double keep1 = x == 1 ? 0.0 : double(rho);
            for (std::size_t s = 0; s < kStates; ++s) {
                const double c0 = cost[s] + keep0;
                const double c1 = cost[s ^ column] + keep1;
                next[s] = c1 < c0 ? c1 : c0;
                bits[s >> 3] = static_cast<std::uint8_t>(bits[s >> 3] | (std::uint8_t(c1 < c0) << (s & 7)));
            }
            cost = next;
        }
        // Fin del bloque: el bit bajo del estado debe ser el del mensaje y se descarta.
        next.fill(kInfinity);
        for (std::size_t s = 0; s < kStates / 2; ++s) next[s] = cost[(s << 1) | message[i]];
        cost = next;
    }
    if (std::isinf(cost[0])) return false;

    // Retroceso desde el estado final 0.
    std::size_t state = 0, e = n;
    for (std::size_t i = m; i-- > 0;) {
        state = (state << 1) | message[i];
        const std::uint8_t mask = block_mask(i, m);
        const std::size_t begin = block_start(i, n, m);
        while (e > begin) {
            --e;
            const std::uint8_t bit = (path[e * kPathBytes + (state >> 3)] >> (state & 7)) & 1u;
            out[e] = bit;
            if (bit) state ^= static_cast<std::size_t>(table[e - begin] & mask);
        }
    }
    return true;
}

void extract_segment(const std::uint8_t* lsb, const Segment& seg, std::uint8_t* out) {
    const std::size_t n = seg.elements, m = seg.bits;
    const auto& table = columns();
    std::size_t state = 0;
    for (std::size_t i = 0, e = 0; i < m; ++i) {
        const std::uint8_t mask = block_mask(i, m);
        const std::size_t end = block_start(i + 1, n, m);
        for (std::size_t j = 0; e < end; ++e, ++j)
            if (lsb[e]) state ^= static_cast<std::size_t>(table[j] & mask);
        out[i] = static_cast<std::uint8_t>(state & 1u);
        state >>= 1;
    }
}

}  // namespace

std::size_t sequence_length(std::size_t available, std::size_t message_bits) {
    return message_bits > available / kMaxWidth ? available : message_bits * kMaxWidth;
}

std::optional<std::vector<std::uint8_t>> embed(const std::vector<std::uint8_t>& lsb, const std::vector<float>& costs,
                                               const std::vector<std::uint8_t>& message) {
    require(lsb.size() == costs.size() && !message.empty() && lsb.size() >= message.size(), ErrorCode::Internal,
            "Parametros de STC invalidos");
    const std::vector<Segment> segments = plan(lsb.size(), message.size());
    std::vector<std::uint8_t> out(lsb.size());
    std::atomic<std::size_t> next{0};
    std::atomic<bool> ok{true};
    std::exception_ptr failure;
    std::mutex failure_mutex;

    // Una excepcion que saliera de un hilo cerraria el programa: se relanza al terminar.
    auto worker = [&] {
        try {
            for (std::size_t i = next++; i < segments.size(); i = next++) {
                const Segment& s = segments[i];
                if (!embed_segment(&lsb[s.first_element], &costs[s.first_element], &message[s.first_bit], s,
                                   &out[s.first_element]))
                    ok = false;
            }
        } catch (...) {
            const std::lock_guard lock(failure_mutex);
            if (!failure) failure = std::current_exception();
        }
    };
    const std::size_t threads =
        std::min<std::size_t>(segments.size(), std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (std::size_t t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
    if (failure) std::rethrow_exception(failure);
    if (!ok) return std::nullopt;
    return out;
}

std::vector<std::uint8_t> extract(const std::vector<std::uint8_t>& lsb, std::size_t message_bits) {
    require(message_bits > 0 && lsb.size() >= message_bits, ErrorCode::Internal, "Parametros de STC invalidos");
    std::vector<std::uint8_t> out(message_bits);
    for (const Segment& s : plan(lsb.size(), message_bits))
        extract_segment(&lsb[s.first_element], s, &out[s.first_bit]);
    return out;
}

}  // namespace icpu::stego::stc
