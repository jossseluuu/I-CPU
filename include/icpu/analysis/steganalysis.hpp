#pragma once
// Estegoanalisis: tecnicas clasicas para DETECTAR ocultacion LSB.
// Se incluyen para evaluar el propio sistema (un buen esquema debe resistirlas).
//
// 1. Ataque chi-cuadrado (Westfeld y Pfitzmann, 1999).
//    LSB replacement iguala las frecuencias de cada "par de valores" (2i, 2i+1).
//    Se contrasta la hipotesis "el histograma tiene pares igualados":
//    p ~ 1 => muy probablemente hay datos ocultos; p ~ 0 => imagen natural.
//    Aplicado sobre porciones crecientes de la imagen detecta tambien la
//    insercion secuencial (p alto al principio y cae donde acaba el mensaje).
//
// 2. Analisis RS (Fridrich, Goljan y Du, 2001).
//    Clasifica grupos de pixeles en Regulares/Singulares segun como cambia su
//    "suavidad" al voltear LSBs, y ESTIMA la fraccion de muestras usadas.
//
// 3. Chi-cuadrado sobre el histograma de coeficientes DCT (deteccion de JSteg).

#include <vector>

#include "icpu/image/raster.hpp"
#include "icpu/jpeg/jpeg.hpp"

namespace icpu::analysis {

struct ChiSquareResult {
    double statistic = 0;
    unsigned degrees_of_freedom = 0;
    double p_value = 0;  // probabilidad de que haya insercion
};

// Histograma de 256 o 65536 bins -> test de pares de valores.
ChiSquareResult chi_square_pairs(const std::vector<std::uint64_t>& histogram);

struct ChiSquareSweep {
    std::vector<double> fraction;  // porcion de la imagen analizada (0..1]
    std::vector<double> p_value;   // acumulado: muestras desde el principio hasta `fraction`
    std::vector<double> window_p;  // por ventanas: solo las muestras entre la porcion anterior y esta
};

// Chi-cuadrado sobre las muestras de color en orden de barrido (filas de arriba abajo), de dos formas:
//  - acumulado (Westfeld-Pfitzmann): prefijos crecientes. Senala una insercion secuencial que empieza
//    al principio de la imagen, pero con muchas muestras basta un trozo sin tocar al inicio (como la
//    cabecera del formato v3, escrita con STC) para que p caiga a 0 en todo el barrido.
//  - por ventanas: cada porcion por separado. Marca p ~ 1 justo en las porciones con el mensaje,
//    empiece donde empiece.
ChiSquareSweep chi_square_sweep(const image::Raster& raster, unsigned steps = 20);
ChiSquareResult chi_square_image(const image::Raster& raster);

struct RsResult {
    double estimated_rate = 0;  // fraccion estimada de muestras con mensaje (0..1)
    double rm = 0, sm = 0, r_m = 0, s_m = 0;  // fracciones R_M, S_M, R_-M, S_-M
};

RsResult rs_analysis(const image::Raster& raster);

ChiSquareResult chi_square_dct(const jpeg::CoefImage& image);

// Probabilidad de la cola superior de la chi-cuadrado (funcion gamma incompleta).
double chi_square_cdf(double x, double k);

}  // namespace icpu::analysis
