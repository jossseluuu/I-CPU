# 0002 · Sal derivada de la portadora y parámetros de KDF fijos

**Estado:** aceptada (decisión original del diseño, documentada el 2026-10-30)

## Contexto
Argon2id necesita una sal, y el receptor tiene que usar la misma. Guardarla en la imagen exige
leerla antes de saber dónde está, pero las posiciones dependen de la clave, que depende de la
sal. Es un círculo. Una sal fija, en cambio, permitiría precalcular ataques de diccionario
válidos para todas las imágenes.

## Decisión
- La sal es `BLAKE2b("icpu/v1/salt/" ‖ dominio ‖ nº de ranuras ‖ material invariante)`, donde
  el material invariante es lo que **no cambia al insertar**: los 4 bits altos de cada muestra,
  o el par {2m, 2m+1} en DCT y paleta.
- Los parámetros de Argon2id (64 MiB, 3 pasadas, 1 carril) son fijos en la versión 1 del formato.

## Consecuencias
- Cada imagen tiene su propia sal y no se almacena nada en claro para derivar la clave.
- Las portadoras **deben** cumplir la invariante 2 (`carrier.hpp`). Por eso las exclusiones van
  por pares y el LSB matching limita el acarreo al bit 3.
- Cambiar los parámetros de la KDF exige una versión nueva del formato.
- Dos portadoras con el mismo material invariante comparten sal. Es aceptable: el nonce del
  AEAD es aleatorio en cada inserción.
