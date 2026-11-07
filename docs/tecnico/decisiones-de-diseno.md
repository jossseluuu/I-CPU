# Decisiones de diseño

> **Para quién:** quien quiera entender *por qué* I-CPU está hecho así antes de cambiarlo.
> Cada decisión explica el problema, lo que se eligió, lo que cuesta y lo que se descartó.

Este capítulo sustituye al antiguo registro de ADR (`docs/adr/`). Las decisiones conservan su
número original para que las referencias del [CHANGELOG](../../CHANGELOG.md) sigan teniendo sentido.

| Nº | Decisión | Estado |
| --- | --- | --- |
| [D1](#d1-compilar-en-windows-con-msvc) | Compilar en Windows con MSVC | Vigente |
| [D2](#d2-sal-derivada-de-la-portadora-sustituida) | Sal derivada de la portadora | Sustituida por D7 |
| [D3](#d3-la-gui-es-un-servidor-web-local-con-token) | La GUI es un servidor web local con token | Vigente, ampliada en 2.0 |
| [D4](#d4-análisis-y-experimentos-dentro-de-la-biblioteca) | Análisis y experimentos dentro de la biblioteca | Vigente |
| [D5](#d5-imágenes-de-prueba-generadas-en-código) | Imágenes de prueba generadas en código | Vigente, ampliada por D7 |
| [D6](#d6-formato-v2-cabecera-cifrada-sustituida) | Formato v2: cabecera cifrada | Sustituida por D7 |
| [D7](#d7-formato-v3-sal-aleatoria-guardada-en-la-imagen) | Formato v3: sal aleatoria guardada en la imagen | Vigente |
| [D8](#d8-inserción-adaptativa-con-stc-por-defecto) | Inserción adaptativa con STC, por defecto | Vigente |
| [D9](#d9-barrido-chi-cuadrado-acumulado-y-por-ventanas) | Barrido chi-cuadrado acumulado y por ventanas | Vigente (2.0) |

---

## D1. Compilar en Windows con MSVC

**Problema.** El proyecto empezó con MinGW-w64 (GCC). En el equipo de desarrollo está activado
**Smart App Control** de Windows 11, que bloquea ejecutables sin firma o sin reputación. Bloqueaba
`cmake.exe`, `as.exe` y `ld.exe` de MinGW (error `0xC0E90002`), así que no se podía compilar nada.
Desactivarlo no es buena idea: no se puede volver a activar sin reinstalar Windows.

**Decisión.** En Windows se compila con **MSVC** de las *Build Tools para Visual Studio*, con el
CMake y el Ninja que trae Visual Studio: todo está firmado por Microsoft. `scripts\build.bat`
localiza Visual Studio con `vswhere`, carga `vcvars64.bat` y usa los presets `msvc-release` o
`msvc-debug`.

**Consecuencias.**
- Basta con instalar una sola cosa.
- El código sigue siendo portable: los presets `release`, `debug` y `sanitize` usan GCC o Clang
  en Linux, y la CI compila en los dos sistemas.
- MSVC con `/W4 /permissive-` destapó un fallo real: un `setjmp` encadenado con `||` en
  `jpeg.cpp`, que era comportamiento indefinido.
- Smart App Control sigue bloqueando *a veces* los ejecutables recién compilados, que no van
  firmados. Se esquiva volviendo a enlazar (cambia el hash); ver [Instalación](../guias/instalacion.md#problemas-frecuentes).

---

## D2. Sal derivada de la portadora (sustituida)

**Problema.** Argon2id necesita una sal, y el receptor tiene que usar la misma. Guardarla en la
imagen parecía circular: hay que leerla antes de saber dónde está, y las posiciones dependen de la
clave, que depende de la sal.

**Decisión original (v1 y v2).** La sal se calculaba con lo que la inserción **no** cambia: los 4
bits altos de cada muestra, o el par {2m, 2m+1} en DCT y paleta. Los parámetros de Argon2id eran fijos.

**Por qué se sustituyó.** En la 1.5 se midieron dos problemas graves, descritos en [D7](#d7-formato-v3-sal-aleatoria-guardada-en-la-imagen):
la misma imagen con la misma contraseña daba **siempre las mismas posiciones**, y el LSB matching
dejaba un **peine de periodo 16** en el histograma. La lectura de v1 y v2 se conserva en
`src/stego/legacy.cpp`.

---

## D3. La GUI es un servidor web local con token

**Problema.** Se quería una interfaz gráfica sin un toolkit pesado (Qt, wxWidgets). cpp-httplib ya
estaba en el proyecto. El riesgo de un servidor local es que **cualquier web abierta en el
navegador** o cualquier proceso del equipo intente usar su API, que maneja contraseñas y ficheros.

**Decisión.**
- `icpu-gui` escucha **solo en 127.0.0.1**, en un puerto libre (o el de `--port`).
- Cada arranque genera un **token de 128 bits** que llega a la web en el fragmento de la URL
  (`#t=…`), que nunca se envía al servidor ni queda en registros. Toda llamada a `/api/` exige la
  cabecera `X-ICPU-Token`, comparada en tiempo constante. Otra web no puede añadir esa cabecera sin
  una verificación CORS que nunca se concede.
- Se rechaza cualquier `Host` distinto de `127.0.0.1:puerto` o `localhost:puerto` (*DNS rebinding*).
- La web se **embebe en el ejecutable** al compilar y se sirve con CSP estricta, sin scripts
  inline y sin `innerHTML` con datos.

**Ampliación de la 2.0: ciclo de vida.** El botón de apagado de la 1.5 fallaba en silencio: el
navegador enviaba un formulario vacío, cpp-httplib lo rechazaba con un 400 y la web mostraba
«cerrado» con el servidor todavía en marcha. Desde la 2.0:
- el apagado comprueba la respuesta y, si falla, lo dice;
- el servidor también se cierra con Ctrl+C, al cerrar la consola o cuando no queda **ninguna
  pestaña abierta** (cada pestaña avisa al abrirse, cada 20 s y al cerrarse);
- si hay una operación larga en marcha, no se espera más de 2 s: se cancela;
- en Windows solo hay **una instancia**: abrir `icpu-gui` otra vez abre una pestaña nueva de la
  que ya existe, sin que el token salga del proceso.

Ver el [diagrama del ciclo de vida](interfaz-web.md#ciclo-de-vida).

**Consecuencias.** Un único `.exe` portable y una interfaz con HTML, CSS y JS normales. Si se pierde
la pestaña, hay que abrir de nuevo el enlace de la consola (o volver a ejecutar `icpu-gui`). Hace
falta un navegador moderno.

**Descartado.** Un puerto fijo sin token (cualquier web podría usar la API) y el frontend en una
carpeta junto al `.exe` (habría que distribuirla).

---

## D4. Análisis y experimentos dentro de la biblioteca

**Problema.** `analyze` y `bench` se escribieron primero dentro de la CLI. La GUI necesitaba
exactamente lo mismo, y duplicarlo eran unas 100 líneas que acabarían divergiendo.

**Decisión.** La lógica vive en `include/icpu/report.hpp` (`analyze()`, `bench()`,
`load_pixels()` y los veredictos), junto a `icpu.hpp` y no en `analysis/`: `bench()` usa la API de
alto nivel (`embed`, `inspect`), y `analysis/` es una capa inferior que no debe depender de ella.

**Consecuencias.** La CLI y la GUI solo formatean. `report` tiene sus propias pruebas, y la
biblioteca sirve para hacer experimentos sin ninguna interfaz.

---

## D5. Imágenes de prueba generadas en código

**Problema.** Las pruebas necesitan imágenes de muchos tipos (8 y 16 bits, 1 a 4 canales, JPEG en
gris y en color, GIF, tamaños impares). Guardarlas como binarios pesa y no cubre todas las combinaciones.

**Decisión.** `tests/helpers.hpp` genera imágenes deterministas: ondas suaves más un ruido leve,
que imitan una foto. Usa SplitMix64 y no `std::*_distribution`, cuya salida cambia entre
compiladores. Los JPEG y GIF se crean con los propios códecs.

**Consecuencias.** Con un generador lineal simple, los bits bajos tenían periodos cortos y el
estegoanálisis daba resultados absurdos: de ahí SplitMix64. Como las imágenes son suaves, tienen
pocos coeficientes AC útiles, y las pruebas de JPEG usan 256×192. Las imágenes para el usuario sí
son ficheros (`samples/`).

**Ampliación (D7).** Para comprobar que las versiones antiguas se siguen leyendo hacen falta
imágenes **reales** de esas versiones: `tests/golden/` guarda una por formato y versión.

---

## D6. Formato v2: cabecera cifrada (sustituida)

**Problema.** En v1, la cabecera de 30 bytes (`versión | parámetros | nonce | longitud`) iba **en
claro**, protegida solo por estar en posiciones secretas. En orden secuencial quedaba en las
primeras 240 ranuras: el byte `0x01`, los bits reservados a cero y una longitud plausible formaban
una firma reconocible.

**Decisión (1.5).** `nonce (24 B) | E(versión | parámetros | longitud)`, con `E` = XChaCha20 con una
**clave propia** (`icpu/v2/header`), distinta de la del AEAD, para no reutilizar nunca un par
(clave, nonce). La integridad la da el AEAD, con la cabecera como datos asociados.

**Estado.** La cabecera cifrada sigue igual en v3; lo que cambió fue la sal ([D7](#d7-formato-v3-sal-aleatoria-guardada-en-la-imagen)).

**Descartado.** Cifrar con la clave del AEAD (reutilizaría clave y nonce) y una máscara fija
derivada de la contraseña (la misma en cada inserción: el XOR de dos cabeceras revelaría datos).

---

## D7. Formato v3: sal aleatoria guardada en la imagen

**Problema.** Con la sal derivada de la imagen ([D2](#d2-sal-derivada-de-la-portadora-sustituida)) se midieron en la 1.5:
- **Posiciones repetidas.** Dos inserciones en la misma imagen con la misma contraseña
  compartían 4.220 cambios, frente a 155 esperables por azar. Compararlas delataba los datos.
- **Huella en LSB matching.** Para no tocar los 4 bits altos, el acarreo no podía cruzar las
  fronteras 16m: el histograma quedaba con un peine de periodo 16.
- En DCT, los pares de JSteg modificaban `-1` y no `+1`, y rompían la simetría del histograma.

**Decisión.**
- **Sal aleatoria de 16 bytes** en cada inserción, guardada en W·128 ranuras **públicas** (una
  permutación que solo depende del dominio y del tamaño): se leen antes de tener ninguna clave.
- Sal de Argon2id = BLAKE2b(`icpu/v3/salt/` ‖ dominio ‖ N ‖ sal almacenada).
- Cabecera cifrada de 30 bytes como en v2, en las primeras W·240 ranuras reservadas del orden secreto.
- **Sal y cabecera siempre con STC**, en todos los modos. Escritas bit a bit, en una imagen mitad
  ruido y mitad color liso, 86 de 218 cambios caían en la zona lisa; con STC, ninguno.
- **W** (1 a 64) se deduce de `reserved_count()`, que no cambia al escribir: no se guarda.
- **Pares simétricos en DCT**: se cambia el bit bajo de la magnitud (`2↔3`, `-2↔-3`), sin tocar 0 ni ±1.
- v1 y v2 solo se leen (`src/stego/legacy.cpp`) y se prueban con imágenes reales (`tests/golden`).

**Consecuencias.** Ocultar dos veces da posiciones independientes, y el LSB matching ya no deja
peine. Con una contraseña incorrecta, la extracción ejecuta Argon2id dos veces (prueba también la
sal antigua). JPEG necesita al menos 368 bloques (unos 130×130 px). Las imágenes v3 no se leen
con versiones anteriores. Ver el [formato v3 en detalle](formato.md).

**Descartado.** Guardar los parámetros de Argon2id junto a la sal: un byte con pocos valores
posibles es reconocible aunque se enmascare. Por eso los parámetros siguen siendo fijos.

---

## D8. Inserción adaptativa con STC, por defecto

**Problema.** Hasta la 1.5, cada bit iba a una ranura al azar con el mismo trato para una zona lisa
que para una con textura, y el modo por defecto era la sustitución LSB con `k = auto`, que subía los
bits por ranura sin avisar. Quien no conocía las opciones obtenía lo más detectable.

**Decisión.**
- **Syndrome-Trellis Codes** (Filler, Judas y Fridrich, 2011): `stego/stc.cpp` elige con Viterbi
  los cambios de menor coste total. El receptor solo multiplica los LSB por la matriz de paridad.
  Altura 8 (256 estados), en segmentos de hasta 2²⁰ elementos resueltos en paralelo.
- **Costes por dominio**: HILL (espacial), UERD (JPEG) y distancia de color (paleta). Lo que no se
  puede tocar tiene coste infinito.
- **Modo por defecto `adaptive`** con carga prudente: 0,25 bits por ranura cambiable (0,20 en
  paleta). Si no cabe, da error y explica las alternativas; nunca sube la carga por su cuenta.

**Consecuencias.** Con 12 KB en `samples/paisaje.png`, el modo adaptativo cambia ~5,9 % de las
muestras frente a ~10,4 % de la sustitución, y la tasa RS queda en el nivel de la imagen limpia
(≈ 0,01 frente a ≈ 0,23). Cabe el 25 % de lo que cabía con 1 bit por ranura, y es más lento
(unos 24 s en una foto de 12 MP). Sigue sin ser indetectable frente a detectores modernos.

**Descartado.** J-UNIWARD en JPEG (mejor, pero exige wavelets sobre la imagen descomprimida) y STC
ternario (algo menos de cambios, a costa de un codificador bastante más complejo).

---

## D9. Barrido chi-cuadrado acumulado y por ventanas

**Problema.** El barrido clásico de Westfeld-Pfitzmann calcula el chi-cuadrado sobre prefijos
crecientes de la imagen. Desde el formato v3, las primeras filas de una inserción secuencial son la
cabecera, escrita con STC, que casi no cambia nada. Con tantas muestras el test es tan sensible que
ese trozo intacto basta para que p caiga a 0 en **todo** el barrido: la demo secuencial dejó de
detectarse, y la documentación prometía lo contrario.

**Decisión.** `chi_square_sweep()` devuelve las dos curvas: el **acumulado** de siempre y una
**por ventanas** (cada porción por separado). La CLI, la GUI, el CSV exportado y el JSON muestran
las dos. Una prueba reproduce el caso de la cabecera intacta al principio.

**Consecuencias.** Las ventanas marcan p ≈ 1 justo en la zona del mensaje, empiece donde empiece
(ver la [figura](estegoanalisis.md#barrido-chi-cuadrado)). En imágenes pequeñas o muy ruidosas,
una ventana limpia también puede dar un p alto: con pocas muestras el test discrimina menos.
