# 0007 · Formato v3: sal aleatoria almacenada en la imagen

**Estado:** aceptada (2026-10-31). Sustituye a la [ADR 0002](0002-sal-derivada-de-la-portadora.md)
y a la cabecera de la [ADR 0006](0006-formato-v2-cabecera-cifrada.md) para todo lo que se escribe.

## Contexto

En v1 y v2 la sal de Argon2id salía de la propia imagen. Esa decisión exigía que insertar no
cambiara el material de la sal, y tuvo dos consecuencias medidas en la versión 1.5:

- **Posiciones repetidas.** La misma portadora con la misma contraseña daba las mismas claves y
  posiciones: dos imágenes generadas así compartían 4.220 cambios, frente a 155 esperables por azar.
  Compararlas delataba dónde estaban los datos.
- **Huella en LSB matching.** Para no tocar los 4 bits altos, el acarreo no podía cruzar las
  fronteras 16m. El histograma quedaba con un peine de periodo 16: la masa junto a esas fronteras
  pasaba de 0,99 a 0,63 con la imagen llena, cuando el matching real se queda en 0,99.

Además, el dominio DCT usaba los pares de JSteg: `-1` se modificaba y `+1` no, lo que rompe la
simetría del histograma de coeficientes.

## Decisión

- **Sal aleatoria de 16 bytes** en cada inserción, guardada en W·128 ranuras públicas: una
  permutación cuya clave es el hash del dominio y el número de ranuras. Hacen falta antes de
  tener ninguna clave, así que no pueden ser secretas.
- **Sal de Argon2id** = BLAKE2b(`icpu/v3/salt/` ‖ dominio ‖ N ‖ sal almacenada).
- **Cabecera** de 30 bytes como en v2, en las primeras W·240 ranuras reservadas del orden secreto.
- **Sal y cabecera siempre con STC**, en todos los modos. Escritas bit a bit ignoraban los costes:
  en una imagen de 512×512, mitad ruido y mitad color uniforme, 86 de 218 cambios caían en la zona
  uniforme, y las posiciones de la sal son públicas. El receptor no necesita los costes.
- **W** va de 1 a 64 y se deduce de `reserved_count()`, que no cambia al escribir: no se guarda. Es
  `reserved_count() / 368`, o su octava parte si el cuerpo comparte esas ranuras (`reserved_separate`
  falso), para no quitarle capacidad. W = 1, propio de imágenes muy pequeñas, equivale a escribir
  bit a bit.
- **Ranuras reservadas:** las que ningún modo modifica después de escribirlas (`reserved_usable`).
  Son todas en el dominio espacial, los píxeles con par completo en paleta y los coeficientes DC
  en JPEG. En JPEG esto significa que hacen falta al menos 368 bloques.
- **Pares simétricos en DCT**: se sustituye el LSB de la magnitud (`2↔3`, `-2↔-3`), con 0 y ±1 excluidos.
- **Compatibilidad:** v1 y v2 solo se leen, en `src/stego/legacy.cpp`, que conserva su sal y sus
  ranuras. Ya no existe código que las escriba; se prueban con imágenes reales de esas versiones
  (`tests/golden`), lo que amplía la [ADR 0005](0005-imagenes-de-prueba-generadas.md).

## Consecuencias

- Ocultar dos veces en la misma imagen da posiciones independientes.
- LSB matching es matching real y no deja peine.
- Cualquier imagen, con o sin mensaje, "tiene" una sal: es el síndrome de sus LSB, un valor que
  existe aunque nadie lo haya escrito. No basta para distinguirlas: en una foto, esos LSB ya
  parecen ruido.
- En esa misma imagen de prueba, los cambios en la zona uniforme pasan de 86 a 0. Si la imagen es
  lisa por completo no hay dónde esconderlos, y por eso el programa avisa.
- Con una contraseña incorrecta, la extracción ejecuta Argon2id dos veces, porque debe probar
  también la sal antigua. Los mensajes v3 correctos no pagan ese coste.
- Las imágenes v3 no se pueden leer con versiones anteriores.

## Alternativas descartadas

- **Guardar los parámetros de Argon2id junto a la sal.** Un byte con valores en un conjunto pequeño
  es reconocible aunque se enmascare con un hash de la sal: un atacante calcula el mismo hash y
  comprueba si el valor cae en ese conjunto. Por eso los parámetros siguen siendo fijos; un coste
  mayor en el futuro exigirá probar varios valores al extraer.
- **Mantener la sal derivada de la imagen con más exclusiones.** Cualquier resumen de bits altos
  se rompe con el acarreo de ±1, y los modos con STC modifican muestras en lugares no previsibles.
