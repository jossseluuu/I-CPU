# Arquitectura de I-CPU

Cómo funciona I-CPU por dentro: desde que el usuario pulsa "Ocultar" hasta que los bits
quedan repartidos por la imagen, y por qué está hecho así.

## 1. Capas

```
 apps/cli (icpu.exe)        apps/gui (icpu-gui.exe: HTTP local + web embebida)
            \                      /
             icpu.hpp  ·  report.hpp          <- API de alto nivel (embed, extract, inspect, analyze, bench)
                     |
             stego/  (documento -> portadora -> motor)
            /        |          \
     image/        jpeg/        gif/            <- acceso a píxeles, coeficientes DCT, índices de paleta
            \        |          /
        crypto/   compress/   core/   analysis/
```

- Las **interfaces** solo interpretan argumentos y formatean resultados. Toda la lógica,
  incluidos los informes de análisis y los experimentos, está en la biblioteca `icpu_core`.
- El **motor** (`stego/engine`) no sabe de formatos de imagen: trabaja con una abstracción,
  la *portadora*.

## 2. La portadora: una secuencia de "ranuras"

Una portadora (`stego/carrier.hpp`) es una secuencia de N ranuras en las que se pueden
escribir *k* bits. Cada dominio la implementa a su manera:

| Dominio | Clase | Ranura | k | Ranuras excluidas |
|---|---|---|---|---|
| espacial | `PixelCarrier` | una muestra de color R, G, B o gris (nunca alfa) | 1–4 | ninguna |
| DCT | `DctCarrier` | un coeficiente cuantizado | 1 | DC, 0 y ±1 (en adaptativo: DC y 0) |
| paleta | `PaletteCarrier` | un píxel de cualquier fotograma | 1 | color transparente, pares incompletos |

Toda portadora debe cumplir estas **invariantes**, porque el receptor tiene que ver exactamente
lo mismo que el emisor:

1. Escribir con `write` no cambia qué ranuras son utilizables. Por eso las exclusiones se hacen
   **por pares**: cambiar el LSB nunca saca un valor de su par.
2. Las ranuras **reservadas**, donde van la sal y la cabecera, no cambian con ningún modo. Son todas
   en espacial, los píxeles con par completo en paleta y los coeficientes DC en JPEG.
3. `read` devuelve lo último que se escribió, y `flip` invierte el LSB que lee `read`.
4. `read` está definido en todas las ranuras, también en las que no se pueden cambiar: el receptor
   de STC lee la secuencia completa sin saber cuáles eran.

**Documentos** (`stego/document.hpp`): abren la imagen en el dominio que corresponde al
formato de **salida** y saben volver a serializarla.

| Salida | Dominio | Qué hace |
|---|---|---|
| PNG/BMP/TGA/QOI/PNM | espacial | Decodifica, adapta la imagen al formato (*canonicaliza*: p. ej. BMP no admite 16 bits) y avisa de cada cambio |
| JPEG | DCT | Si la portadora ya es JPEG, lee sus coeficientes **sin recomprimir**; si no, la comprime una vez (calidad 90) y oculta después |
| GIF | paleta | Exige una portadora GIF; conserva paletas, animación y extensiones |

## 3. Qué se esconde: el contenedor

```
mensaje ──► carga útil ──► (DEFLATE si reduce) ──► XChaCha20-Poly1305 ──► bits en la imagen
```

**Carga útil** (`stego/payload`): `flags | longitud del nombre | nombre | tamaño original | datos`.
Se comprime *antes* de cifrar, porque después de cifrar ya no comprime.

**Contenedor** (`stego/engine.hpp`, versión 3, la que se escribe desde la 2.0):

```text
SAL (16 B)       en W·128 ranuras públicas (solo dependen del dominio y del tamaño)
CABECERA (30 B)  en W·240 ranuras reservadas del orden secreto: nonce (24 B) | E(versión | parámetros | longitud)
CUERPO           en el orden secreto, con STC o a k bits por ranura: cifrado || MAC (16 B)
E = XChaCha20(clave de cabecera, nonce), 6 bytes
```

- Lo único que se lee sin contraseña es la sal, y cualquier imagen "tiene" una: es el síndrome de
  sus LSB, un valor que existe aunque nadie lo haya escrito.
- La cabecera descifrada se autentica como *datos asociados* del AEAD: si cambia un solo bit
  (incluido el nonce), el descifrado falla.
- Sal y cabecera van siempre con STC, sea cual sea el modo: el receptor no necesita los costes ni
  conocer antes los parámetros. W va de 1 a 64 y sale de `reserved_count()`, así que no se guarda.
  En `samples/paisaje.png`, con un mensaje corto, toda la inserción cambia 85 ranuras.

Las versiones 1 y 2 solo se leen (`stego/legacy.cpp`). Tenían la sal derivada de la imagen; v1,
además, la cabecera en claro. Ver [ADR 0007](adr/0007-formato-v3-sal-almacenada.md).

## 4. Claves y posiciones

```
sal argon = BLAKE2b("icpu/v3/salt/" ‖ dominio ‖ N ‖ sal almacenada)
maestra   = Argon2id(contraseña, sal argon, 64 MiB, 3 pasadas, ad = "icpu/v1/argon2id")
cifrado     = BLAKE2b(clave = maestra, "icpu/v1/encryption")
posiciones  = BLAKE2b(clave = maestra, "icpu/v1/placement")
cabecera    = BLAKE2b(clave = maestra, "icpu/v2/header")
```

- La **sal es aleatoria en cada inserción**, así que ocultar dos veces en la misma imagen con la
  misma contraseña da claves y posiciones independientes.
- Los parámetros de Argon2id son **fijos**: guardarlos dejaría un valor reconocible.
- **Orden aleatorio** (`stego/slot_order`): una permutación Fisher-Yates "perezosa" alimentada
  por ChaCha20 con la clave de posiciones. Genera la permutación solo hasta donde hace falta,
  con una tabla hash mientras se usa poco de la imagen y un vector denso a partir de 1/8.
- **Extracción**: lee la sal, deriva las claves y lee la cabecera. Si no es plausible o el AEAD
  falla, repite con la sal antigua para probar v2 y v1. Una contraseña incorrecta, una imagen sin
  mensaje y una imagen alterada dan **el mismo error** a propósito.

## 5. Modos de escritura

- **Adaptativo** (`adaptive`, por defecto): STC con altura 8 (`stego/stc`). Viterbi elige los
  cambios de menor coste total; el receptor solo necesita los LSB y la matriz de paridad. Costes:
  HILL en espacial, UERD en JPEG y distancia entre colores en paleta, con coste infinito para lo
  que no se puede tocar. La carga se limita a 0,25 bits por ranura cambiable (0,20 en paleta).
  Ver [ADR 0008](adr/0008-stc-adaptativo-por-defecto.md).
- **LSB matching** (`match`, solo espacial): si el bit no coincide, se suma o resta 1 al azar. No
  deja el rastro de pares en el histograma, pero reparte los cambios sin mirar la imagen.
- **Sustitución LSB** (`replace`, hasta 4 bits por ranura): iguala las frecuencias de cada par, y
  eso es justo lo que detectan el chi-cuadrado y el análisis RS. En JPEG usa pares simétricos por
  magnitud (`2↔3`, `-2↔-3`).

Con `replace`, `k = auto` elige el **menor** *k* con el que cabe el mensaje. El modo adaptativo
nunca sube la carga: si el mensaje no cabe, da un error que explica las alternativas.

## 6. Estegoanálisis y experimentos

`report.hpp` reúne lo que usan la CLI y la GUI:

- `analyze()`: chi-cuadrado (Westfeld-Pfitzmann) y su barrido por porciones, análisis RS
  (Fridrich), y chi-cuadrado sobre coeficientes DCT para JPEG.
- `bench()`: oculta mensajes aleatorios variando *k*, modo, orden y llenado, y mide PSNR, SSIM
  y detección. Usa Argon2id reducido porque el experimento no mide la KDF.

Resultados con la portadora de ejemplo (`experiments\run_bench.bat`):

| Mensaje | Modo | Muestras cambiadas | PSNR | Tasa RS |
| --- | --- | --- | --- | --- |
| 14 KiB | adaptativo | 7,1 % | 59,6 dB | 0,02 |
| 14 KiB | sustitución | 12,5 % | 57,2 dB | 0,24 |
| 14 KiB | matching | 12,5 % | 57,1 dB | 0,01 |

Que RS no salte con matching no lo hace seguro: RS busca los pares de la sustitución, no los
cambios en zonas lisas, que es lo que explotan los detectores modernos y lo que evita el modo adaptativo.

- **Secuencial**: el barrido chi-cuadrado delata dónde termina el mensaje.
- **Paleta**: la distorsión depende de lo "densa" que sea la paleta. Con la paleta de cubo
  6×7×6 de la demo, los colores vecinos distan unos 43 niveles y el PSNR baja a unos 36 dB.
  Con paletas adaptativas de fotos reales es mucho menor.

## 7. GUI

Ver [ADR 0003](adr/0003-gui-servidor-local-con-token.md). Es un servidor cpp-httplib en
127.0.0.1 con la web (`apps/gui/web/`) convertida en arrays de bytes al compilar
(`embed_assets.cmake`). El servidor es una biblioteca (`icpu_gui_server`) para que las pruebas
lo arranquen en un hilo (`tests/test_gui.cpp`).

| Ruta | Entrada (multipart) | Respuesta |
| --- | --- | --- |
| `POST /api/info` | `image` | JSON: formato, dimensiones, capacidades |
| `POST /api/embed` | `cover`, `message` o `payload`, `password`, opciones | JSON: imagen (base64), estadísticas, PSNR/SSIM, mapa de diferencias y vistas previas PNG si el navegador no sabe mostrar el formato |
| `POST /api/extract` | `image`, `password`, `layout` | JSON: texto o fichero (base64) |
| `POST /api/analyze` | `image`, `cover` opcional | JSON: tests, histogramas, planos de bits, calidad |
| `POST /api/bench` | `cover`, listas de parámetros | **NDJSON en streaming**: líneas `progress`, y al final `result` o `error` |
| `POST /api/preview` | `image` | PNG (para QOI, TGA, PNM, PSD…) |
| `GET /api/version`, `POST /api/shutdown` | — | JSON |

La web no usa bibliotecas externas. El comparador antes/después y la lupa trabajan sobre
`<canvas>`, y las gráficas son SVG generado en el propio JavaScript (con exportación a PNG).
El historial y las descargas viven solo en memoria: guardar nombres de fichero en el navegador
dejaría rastro del uso de la herramienta.

## 8. Limitaciones conocidas

- Cualquier edición de la imagen (recomprimir, redimensionar, recortar, convertir) destruye el mensaje.
- El análisis clásico incluido no es el estado del arte: detectores modernos (SRM, redes neuronales)
  detectan cargas mucho menores, y el modo adaptativo no se ha evaluado frente a ellos.
- Quien tenga la imagen original puede compararla con la que lleva el mensaje.
- El modo adaptativo es lento en imágenes grandes: unos 24 s con 12 MP.
- La descompresión de stb falla con un flujo de 0 bytes. Hoy no se alcanza: un mensaje vacío
  nunca se comprime.
- Las imágenes v1 y v2 conservan sus debilidades. Si importa, conviene volver a ocultar esos mensajes.
- El análisis RS se vuelve inestable cerca del 100 % de llenado (puede devolver ≈ 0 con la
  imagen llena); el chi-cuadrado sí lo detecta en ese caso.
- Sin auditoría de seguridad independiente.
