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
// Invariantes que toda portadora debe cumplir (formato v3):
//   1. usable(i) no cambia con write().
//   2. reserved_usable(i) no cambia ni con write() ni con flip(): la sal y la
//      cabecera van en esas ranuras y el receptor debe encontrar las mismas.
//   3. read(i, k) devuelve lo ultimo escrito con write(i, v, k), y flip(i)
//      invierte read(i, 1).
//   4. read(i, 1) esta definido en TODAS las ranuras, tambien en las que no se
//      pueden cambiar: el receptor de STC lee la secuencia completa.

#include <cstdint>
#include <string_view>
#include <vector>

#include "icpu/crypto/hash.hpp"
#include "icpu/crypto/random.hpp"

namespace icpu::stego {

enum class EmbedMode {
    Replace,  // LSB replacement: se sustituyen los k bits bajos
    Match,    // LSB matching (+-1): misma informacion, histograma mas natural (k = 1)
    Adaptive  // STC + costes adaptativos: cambia pocas ranuras y en zonas con textura (k = 1)
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

    // Ranuras donde pueden ir la sal y la cabecera, que se escriben con write(.., 1, ..).
    virtual bool reserved_usable(std::uint64_t slot) const = 0;
    virtual std::uint64_t reserved_count() const = 0;
    // Verdadero si el cuerpo nunca puede usar las ranuras reservadas.
    virtual bool reserved_separate() const { return false; }
    // Coste de cambiar cada ranura reservada; debe ser finito en todas ellas.
    virtual std::vector<float> reserved_costs() const { return embedding_costs(); }

    // Modo adaptativo. embedding_costs() da el coste de cambiar cada ranura
    // (infinito = no se puede). changeable_count() cuenta las de coste finito y
    // safe_rate() son los bits por ranura cambiable que se consideran prudentes.
    virtual std::vector<float> embedding_costs() const = 0;
    virtual std::uint64_t changeable_count() const = 0;
    virtual double safe_rate() const = 0;
    virtual bool flip(std::uint64_t slot, crypto::RandomBits& rng) = 0;

    // Fraccion de la imagen sin textura, donde cualquier cambio queda aislado.
    virtual double flat_share() const { return 0; }

    // Lectura de los formatos v1 y v2. La sal salia de la imagen, asi que las
    // portadoras deben seguir calculandola, y sus ranuras pueden diferir de v3.
    virtual void salt_material(crypto::Blake2b256& hash) const = 0;
    virtual bool legacy_usable(std::uint64_t slot) const { return usable(slot); }
    virtual std::uint64_t legacy_usable_count() const { return usable_count(); }
};

}  // namespace icpu::stego
