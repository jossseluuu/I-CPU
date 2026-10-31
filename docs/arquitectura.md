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
| DCT | `DctCarrier` | un coeficiente AC cuantizado | 1 | DC, valores 0 y 1, par {−1024, −1023} |
| paleta | `PaletteCarrier` | un píxel de cualquier fotograma | 1 | color transparente, pares incompletos |

Toda portadora debe cumplir tres **invariantes**, porque el receptor tiene que ver exactamente
lo mismo que el emisor:

1. Escribir no cambia qué ranuras son utilizables. Por eso las exclusiones se hacen **por pares**
   {2m, 2m+1}: cambiar el LSB nunca saca un valor de su par.
2. El material del que se deriva la sal no cambia al escribir. En el dominio espacial son los
   4 bits altos de cada muestra; en DCT y en paleta, el par al que pertenece cada valor.
3. `read` devuelve lo último que se escribió con `write`.

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

**Contenedor** (`stego/engine.hpp`, versión 2, la que se escribe desde la 1.5):

```text
CABECERA (30 bytes, siempre a 1 bit por ranura)                 CUERPO (k bits por ranura)
| nonce (24 B, aleatorio) | E(versión | parámetros | longitud) |  | cifrado || MAC (16 B) |
E = XChaCha20(clave de cabecera, nonce), 6 bytes
```

- En la imagen solo hay un nonce aleatorio y texto cifrado: sin la contraseña es indistinguible
  de ruido, **también en orden secuencial**.
- La cabecera descifrada se autentica como *datos asociados* del AEAD: si cambia un solo bit
  (incluido el nonce), el descifrado falla.
- La cabecera va siempre a 1 bit por ranura porque el receptor aún no conoce *k*.
- La clave de cabecera es distinta de la del AEAD, así que el mismo nonce nunca se usa dos
  veces con la misma clave. Ver [ADR 0006](adr/0006-formato-v2-cabecera-cifrada.md).

La **versión 1** (I-CPU 1.0) tenía la misma cabecera pero en claro: `versión | parámetros |
nonce | longitud`. Se sigue pudiendo leer.

## 4. Claves y posiciones

```
sal    = BLAKE2b("icpu/v1/salt/" ‖ dominio ‖ N ‖ material invariante de la portadora)
maestra = Argon2id(contraseña, sal, 64 MiB, 3 pasadas, ad = "icpu/v1/argon2id")
cifrado     = BLAKE2b(clave = maestra, "icpu/v1/encryption")
posiciones  = BLAKE2b(clave = maestra, "icpu/v1/placement")
cabecera    = BLAKE2b(clave = maestra, "icpu/v2/header")      (formato v2)
```

- La **sal sale de la propia imagen**, así que no hay que guardarla: el receptor la recalcula.
  Es posible gracias a la invariante 2.
- Los parámetros de Argon2id son **fijos** en la versión 1: el receptor no puede leerlos antes
  de derivar la clave, porque la clave decide dónde están los datos.
- **Orden aleatorio** (`stego/slot_order`): una permutación Fisher-Yates "perezosa" alimentada
  por ChaCha20 con la clave de posiciones. Genera la permutación solo hasta donde hace falta,
  con una tabla hash mientras se usa poco de la imagen y un vector denso a partir de 1/8.
- **Extracción**: deriva las claves una sola vez y lee 240 ranuras. Interpreta esos 30 bytes
  primero como cabecera v2 (descifrándolos) y, si no es plausible, como v1. Comprueba versión,
  bits reservados a 0, *k* válido y longitud que quepa, lee el cuerpo y descifra. Una contraseña
  incorrecta, una imagen sin mensaje y una imagen alterada dan **el mismo error** a propósito.

## 5. Modos de escritura

- **Sustitución LSB** (`replace`): se reemplazan los *k* bits bajos. Es simple, pero iguala las
  frecuencias de cada par (2i, 2i+1), y eso es justo lo que detectan el chi-cuadrado y el análisis RS.
- **LSB matching** (`match`, solo espacial y con k = 1): si el bit no coincide, se suma o resta 1
  al azar. Lleva la misma información pero no deja ese rastro en el histograma. El acarreo se limita
  al bit 3 para no tocar los bits altos de los que sale la sal.

Con `k = auto` se elige el **menor** *k* con el que cabe el mensaje, porque menos bits por
ranura significa menos distorsión y menos detectabilidad.

## 6. Estegoanálisis y experimentos

`report.hpp` reúne lo que usan la CLI y la GUI:

- `analyze()`: chi-cuadrado (Westfeld-Pfitzmann) y su barrido por porciones, análisis RS
  (Fridrich), y chi-cuadrado sobre coeficientes DCT para JPEG.
- `bench()`: oculta mensajes aleatorios variando *k*, modo, orden y llenado, y mide PSNR, SSIM
  y detección. Usa Argon2id reducido porque el experimento no mide la KDF.

Resultados típicos con la portadora de ejemplo (`experiments\run_bench.bat`):

- **Sustitución + aleatorio**: RS estima bien la carga (≈ 0,5 al 50 %); el chi-cuadrado solo
  salta cerca del 100 %.
- **Matching**: RS apenas lo detecta (≈ 0,1–0,2 incluso al 100 %).
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
El historial guarda en `localStorage` solo el tipo de operación, el nombre del fichero, los
tamaños y la fecha; las descargas viven en memoria hasta que se recarga la página.

## 8. Limitaciones conocidas

- Cualquier edición de la imagen (recomprimir, redimensionar, recortar, convertir) destruye el mensaje.
- El análisis clásico incluido no es el estado del arte: detectores modernos (SRM, redes
  neuronales) detectan cargas mucho menores.
- `zlib_decompress` de stb falla con un flujo de 0 bytes. Hoy no se alcanza: un mensaje vacío
  nunca se comprime.
- Las imágenes v1 (I-CPU 1.0) tienen la cabecera en claro. Si importa, conviene volver a
  ocultar esos mensajes con la 1.5 para que queden en v2.
- El análisis RS se vuelve inestable cerca del 100 % de llenado (puede devolver ≈ 0 con la
  imagen llena); el chi-cuadrado sí lo detecta en ese caso.
- Sin auditoría de seguridad independiente.
