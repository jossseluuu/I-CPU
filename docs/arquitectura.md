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

**Contenedor** (`stego/engine.hpp`, versión 1):

```
CABECERA (30 bytes, siempre a 1 bit por ranura)          CUERPO (k bits por ranura)
| versión | parámetros | nonce (24 B) | longitud (4 B) |  | cifrado || MAC (16 B) |
```

- La cabecera se autentica como *datos asociados* del AEAD: si cambia un solo bit, el descifrado falla.
- La cabecera va siempre a 1 bit por ranura porque el receptor aún no conoce *k*.

## 4. Claves y posiciones

```
sal    = BLAKE2b("icpu/v1/salt/" ‖ dominio ‖ N ‖ material invariante de la portadora)
maestra = Argon2id(contraseña, sal, 64 MiB, 3 pasadas, ad = "icpu/v1/argon2id")
cifrado     = BLAKE2b(clave = maestra, "icpu/v1/encryption")
posiciones  = BLAKE2b(clave = maestra, "icpu/v1/placement")
```

- La **sal sale de la propia imagen**, así que no hay que guardarla: el receptor la recalcula.
  Es posible gracias a la invariante 2.
- Los parámetros de Argon2id son **fijos** en la versión 1: el receptor no puede leerlos antes
  de derivar la clave, porque la clave decide dónde están los datos.
- **Orden aleatorio** (`stego/slot_order`): una permutación Fisher-Yates "perezosa" alimentada
  por ChaCha20 con la clave de posiciones. Genera la permutación solo hasta donde hace falta,
  con una tabla hash mientras se usa poco de la imagen y un vector denso a partir de 1/8.
- **Extracción**: lee 240 ranuras, comprueba que la cabecera sea plausible (versión, bits
  reservados a 0, *k* válido, longitud que quepa), lee el cuerpo y descifra. Una contraseña
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
(`embed_assets.cmake`). La API es `POST /api/{info,embed,extract,analyze,bench,shutdown}`
con formularios multipart y respuestas JSON (imágenes en base64).

## 8. Limitaciones conocidas

- **La cabecera no está cifrada**: versión, parámetros y longitud van en claro, y solo los
  oculta que sus posiciones son secretas. En orden **secuencial** quedan en las primeras
  240 ranuras con un patrón reconocible. Por eso ese orden es solo para experimentos.
- Cualquier edición de la imagen (recomprimir, redimensionar, recortar, convertir) destruye el mensaje.
- El análisis clásico incluido no es el estado del arte: detectores modernos (SRM, redes
  neuronales) detectan cargas mucho menores.
- `zlib_decompress` de stb falla con un flujo de 0 bytes. Hoy no se alcanza: un mensaje vacío
  nunca se comprime.
- Sin auditoría de seguridad independiente.
