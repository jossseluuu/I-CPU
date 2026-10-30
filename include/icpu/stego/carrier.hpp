#pragma once
// Abstraccion central del sistema: una PORTADORA es una secuencia de N
// "ranuras" (slots) en las que se pueden escribir bits. Cada dominio
// esteganografico implementa esta interfaz:
//
//   - PixelCarrier   : dominio espacial (muestras de color de PNG, BMP, TGA, PNM, QOI)
//   - DctCarrier     : dominio frecuencial (coeficientes DCT cuantizados de JPEG)
//   - PaletteCarrier : dominio de paleta (indices de color de GIF, estilo EzStego)
//
// El motor (engine.hpp) no conoce el formato de imagen: solo usa esta interfaz.
// Invariantes que toda portadora debe cumplir:
//   1. usable(i) no cambia al escribir en ninguna ranura (el receptor debe ver
//      exactamente las mismas ranuras utilizables que el emisor).
//   2. salt_material() no cambia al escribir (la sal se recalcula al extraer).
//   3. read(i, k) devuelve lo ultimo escrito con write(i, v, k).

#include <cstdint>
#include <string_view>

#include "icpu/crypto/hash.hpp"
#include "icpu/crypto/random.hpp"

namespace icpu::stego {

enum class EmbedMode {
    Replace,  // LSB replacement: se sustituyen los k bits bajos
    Match     // LSB matching (+-1): misma informacion, histograma mas natural (k = 1)
};

class Carrier {
public:
    virtual ~Carrier() = default;

    virtual std::string_view domain() const = 0;
    virtual std::uint64_t slot_count() const = 0;
    virtual bool usable(std::uint64_t slot) const = 0;
    virtual std::uint64_t usable_count() const = 0;
    virtual unsigned max_bits_per_slot() const = 0;
    virtual bool supports_matching() const = 0;

    virtual std::uint32_t read(std::uint64_t slot, unsigned bits) const = 0;
    // Devuelve true si el valor de la ranura ha cambiado.
    virtual bool write(std::uint64_t slot, std::uint32_t value, unsigned bits, EmbedMode mode,
                       crypto::RandomBits& rng) = 0;

    // Alimenta el hash de la sal con todo lo que es invariante a la insercion.
    virtual void salt_material(crypto::Blake2b256& hash) const = 0;
};

}  // namespace icpu::stego
