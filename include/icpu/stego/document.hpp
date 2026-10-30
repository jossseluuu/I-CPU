#pragma once
// Un DOCUMENTO es una imagen abierta en el dominio adecuado: contiene los
// datos (pixeles, coeficientes o indices), expone su portadora y sabe
// volver a serializarse en su formato.
//
//   formato de salida        documento          portadora
//   PNG/BMP/TGA/PNM/QOI  ->  SpatialDocument -> PixelCarrier
//   JPEG                 ->  JpegDocument    -> DctCarrier
//   GIF                  ->  GifDocument     -> PaletteCarrier

#include <memory>
#include <string>
#include <vector>

#include "icpu/core/bytes.hpp"
#include "icpu/image/format.hpp"
#include "icpu/stego/carrier.hpp"

namespace icpu::stego {

class Document {
public:
    virtual ~Document() = default;
    virtual Carrier& carrier() = 0;
    virtual Bytes serialize() const = 0;
    virtual image::ImageFormat format() const = 0;
    virtual image::Domain domain() const = 0;

    // Explicaciones de las conversiones realizadas (para mostrarlas al usuario).
    std::vector<std::string> notes;
};

// Abre una imagen como PORTADORA para escribirla despues en `output`.
std::unique_ptr<Document> open_cover(ByteSpan cover, image::ImageFormat source, image::ImageFormat output,
                                     int jpeg_quality);

// Abre una imagen ESTEGO (ya escrita) para extraer; el dominio se deduce del formato.
std::unique_ptr<Document> open_stego(ByteSpan stego, std::string_view name_hint = {});

}  // namespace icpu::stego
