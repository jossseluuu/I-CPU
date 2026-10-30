#include "icpu/stego/slot_order.hpp"

#include <limits>
#include <numeric>

#include "icpu/core/error.hpp"

namespace icpu::stego {

namespace {
[[noreturn]] void exhausted() { fail(ErrorCode::Capacity, "Se han agotado las posiciones de la portadora"); }
}  // namespace

std::uint64_t SequentialOrder::next() {
    if (i_ >= n_) exhausted();
    return i_++;
}

RandomOrder::RandomOrder(std::uint64_t n, const crypto::Key256& key) : n_(n), stream_(key) {}

std::uint64_t RandomOrder::get(std::uint64_t k) const {
    if (is_dense_) return dense_[k];
    const auto it = sparse_.find(k);
    return it == sparse_.end() ? k : it->second;
}

void RandomOrder::set(std::uint64_t k, std::uint64_t v) {
    if (is_dense_)
        dense_[k] = static_cast<std::uint32_t>(v);
    else
        sparse_[k] = v;
}

void RandomOrder::densify() {
    dense_.resize(n_);
    std::iota(dense_.begin(), dense_.end(), 0u);
    for (const auto& [k, v] : sparse_) dense_[k] = static_cast<std::uint32_t>(v);
    sparse_.clear();
    sparse_.rehash(0);
    is_dense_ = true;
}

std::uint64_t RandomOrder::next() {
    if (i_ >= n_) exhausted();
    // Paso i de Fisher-Yates: se elige j uniforme en [i, n) y se intercambian a[i] y a[j].
    const std::uint64_t j = i_ + stream_.uniform(n_ - i_);
    const std::uint64_t picked = get(j);
    if (j != i_) set(j, get(i_));
    if (!is_dense_) sparse_.erase(i_);  // la posicion i ya no se volvera a consultar
    ++i_;
    if (!is_dense_ && n_ <= std::numeric_limits<std::uint32_t>::max() && sparse_.size() > n_ / 8) densify();
    return picked;
}

std::unique_ptr<SlotOrder> make_slot_order(Layout layout, std::uint64_t n, const crypto::Key256& key) {
    if (layout == Layout::Sequential) return std::make_unique<SequentialOrder>(n);
    return std::make_unique<RandomOrder>(n, key);
}

}  // namespace icpu::stego
