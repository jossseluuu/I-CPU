# 0006 · Formato v2: cabecera cifrada

**Estado:** aceptada (2026-10-30). Sustituye la cabecera en claro del formato v1.

## Contexto

En el formato v1 la cabecera de 30 bytes era `versión | parámetros | nonce | longitud`, con
versión, parámetros y longitud **en claro**. Solo los protegía que sus posiciones dependen de
la contraseña. Con el orden secuencial quedaban en las primeras 240 ranuras: el byte `0x01`,
los bits reservados a cero y una longitud plausible formaban una firma reconocible. Además,
el comentario de `engine.hpp` afirmaba que no había "ningún dato en claro reconocible", y no
era del todo cierto.

## Decisión

- La cabecera v2 mide lo mismo (30 bytes): `nonce (24 B, aleatorio) | E(versión | parámetros | longitud)`.
- `E` es XChaCha20 con una **clave propia**, `icpu/v2/header`, derivada de la misma clave
  maestra de Argon2id, y el propio nonce. Como la clave es distinta de la del AEAD, ningún
  par (clave, nonce) se reutiliza aunque el nonce sea el mismo.
- La integridad la sigue dando el AEAD: la cabecera descifrada (`nonce | versión | parámetros |
  longitud`) son sus datos asociados.
- **Compatibilidad:** la extracción deriva las claves una sola vez e interpreta los 30 bytes
  primero como v2 y, si no es plausible, como v1. Las imágenes nuevas siempre se escriben en v2.
  `EngineParams::format_version = 1` existe solo para que las pruebas generen imágenes v1.

## Consecuencias

- Nada de lo que hay en la imagen es reconocible sin la contraseña, tampoco en orden secuencial.
  Hay una prueba que lo comprueba: dos inserciones idénticas no comparten ningún byte de cabecera.
- Misma capacidad y mismo coste: extraer no tarda más, porque Argon2id se ejecuta una vez.
- Las imágenes de I-CPU 1.0 se siguen leyendo; las de 1.5 no las lee la 1.0 (error de autenticación).
- Probar v2 antes que v1 tiene una probabilidad despreciable de falso positivo: tendría que
  coincidir la versión (1/256), los bits reservados (1/16), el orden y una longitud válida. Aun
  así, si ocurriera, el AEAD fallaría y se probaría v1.

## Alternativas descartadas

- Cifrar con la clave del AEAD: reutilizaría (clave, nonce) con dos usos distintos.
- Una máscara fija derivada solo de la contraseña: la misma imagen y contraseña darían la misma
  máscara en cada inserción, y el XOR de dos cabeceras revelaría información.
- Romper la compatibilidad con v1: más simple, pero las imágenes de la 1.0 dejarían de leerse.
