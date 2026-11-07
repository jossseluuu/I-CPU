# 0008 · Inserción adaptativa con STC, por defecto

**Estado:** aceptada (2026-10-31).

## Contexto

Hasta la 1.5 cada bit se escribía en una ranura elegida al azar, con el mismo trato para una zona
lisa que para una con textura, y cada bit cambiaba una ranura con probabilidad 1/2. El modo por
defecto era además la sustitución LSB, y `k = auto` subía los bits por ranura sin avisar. Es decir,
quien no conocía las opciones obtenía lo más detectable.

## Decisión

- **Syndrome-Trellis Codes** (Filler, Judas y Fridrich, 2011): `stego/stc.cpp` elige con Viterbi
  los cambios de menor coste total. El receptor solo lee los LSB y multiplica por la matriz de
  paridad: no necesita conocer los costes. Altura 8 (256 estados), con segmentos de hasta 2^20
  elementos que se resuelven en paralelo para limitar la memoria.
- **Costes por dominio**: HILL en espacial, UERD en JPEG y la distancia entre los colores del par
  en paleta. Los ceros de JPEG, el DC y los pares incompletos tienen coste infinito.
- **Modo por defecto `adaptive`** con carga prudente: 0,25 bits por ranura cambiable en espacial y
  JPEG, y 0,20 en paleta. Si el mensaje no cabe, da error y explica cómo elegir otro modo; nunca
  sube la carga por su cuenta.
- `match` y `replace` siguen disponibles para experimentos y cuando se necesita más capacidad, y
  el resultado avisa de que son más detectables.

## Consecuencias

- Con 14 KiB en `samples/paisaje.png`, el modo adaptativo cambia el 7,1 % de las muestras frente al
  12,5 %, y la tasa estimada por RS baja de 0,24 a 0,02.
- **Cabe mucho menos**: el 25 % de lo que cabía con 1 bit por ranura en espacial. Es deliberado.
- **Es más lento**: unos 4 s por imagen de 2 MP y 24 s en una de 12 MP, porque cada elemento recorre
  los 256 estados. La altura 8 es el equilibrio elegido frente a la 10 habitual, que tardaría
  cuatro veces más para una mejora pequeña.
- Sigue sin garantizar indetectabilidad: la carga, si es alta, se puede detectar con
  estegoanálisis moderno. No se ha medido contra detectores tipo SRM o redes neuronales.

## Alternativas descartadas

- **J-UNIWARD en JPEG.** Es mejor que UERD, pero exige calcular wavelets sobre la imagen
  descomprimida. UERD es mucho más sencillo y mejora con claridad el esquema anterior.
- **STC ternario (±1 con dirección elegida).** Reduciría algo más los cambios, a costa de un
  codificador bastante más complejo. La dirección de cada cambio se elige al azar.
