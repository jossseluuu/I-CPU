# Guía de la línea de comandos

> **Para quién:** quien prefiera la consola, quiera automatizar I-CPU en scripts o necesite todas
> las opciones. Referencia completa de `icpu.exe`.

```text
icpu <comando> [opciones]
icpu                       (sin argumentos: modo guiado)
icpu help <comando>        (o icpu <comando> --help)
```

| Comando | Para qué |
| --- | --- |
| [`embed`](#embed-ocultar) | Oculta un texto o un fichero en una imagen |
| [`extract`](#extract-recuperar) | Recupera el mensaje |
| [`info`](#info-capacidad) | Formato de la imagen y cuánto cabe |
| [`analyze`](#analyze-estegoanálisis) | Estegoanálisis y métricas de distorsión |
| [`bench`](#bench-experimentos) | Experimento sistemático |
| [`guided`](#modo-guiado) | Modo guiado paso a paso |
| `formats` | Tabla de formatos soportados |
| `version` | Versión del programa |

## Modo guiado

Ejecutado **sin argumentos** (o con doble clic en `icpu.exe`), I-CPU muestra un menú:

```text
I-CPU 2.0.0 - modo guiado
(para usarlo con argumentos: icpu help)

Que quieres hacer?
  1) Ocultar un mensaje o un fichero en una imagen
  2) Extraer un mensaje
  3) Ver informacion y capacidad de una imagen
  4) Salir
> 1

Imagen portadora (puedes arrastrarla aqui): C:\Fotos\playa.jpg
  JPEG, 4032 x 3024 px ...
Ocultar (1) un texto o (2) un fichero? [1]: 1
Mensaje: Nos vemos a las 8
Imagen de salida [C:\Fotos\playa_copia.jpg]:
Contrasena:
Repite la contrasena:
```

Puedes **arrastrar** las imágenes a la consola para escribir su ruta. Usa siempre el modo
adaptativo y no muestra la contraseña. Si la entrada no es un terminal (una tubería o un script),
`icpu` sin argumentos muestra la ayuda en lugar del menú.

## La contraseña

`embed` y `extract` la toman de la primera de estas fuentes que exista:

| Fuente | Ejemplo | Cuándo usarla |
| --- | --- | --- |
| `--password-file F` | `--password-file clave.txt` | scripts; el fichero puede tener BOM y salto de línea final |
| `-p CLAVE` | `-p "frase larga"` | pruebas (queda en el historial de la consola; I-CPU lo avisa) |
| `ICPU_PASSWORD` | `set ICPU_PASSWORD=...` | scripts en una sesión controlada |
| teclado | (se pide sin eco) | uso normal; en `embed` se pide dos veces |

En Windows, la contraseña se normaliza a Unicode NFC y la variable de entorno se lee en UTF-16, así
que las tildes y la ñ funcionan igual en la consola, en la GUI y en Linux.

## `embed`: ocultar

```text
icpu embed <portadora> -o <salida> (-m TEXTO | -f FICHERO | --stdin) [opciones]
```

| Opción | Descripción |
| --- | --- |
| `-o, --output F` | imagen resultante (obligatorio). Su extensión decide el formato y el dominio |
| `-m, --message T` | ocultar un texto |
| `-f, --file F` | ocultar un fichero (se recupera con su nombre) |
| `--stdin` | ocultar lo que llegue por la entrada estándar |
| `--format EXT` | forzar el formato de salida |
| `--mode M` | `adaptive` (por defecto), `match` o `replace` |
| `--bits auto\|1..4` | bits por ranura; solo con `replace` |
| `--layout L` | `random` (por defecto) o `sequential` (solo experimentos) |
| `--quality 1..100` | calidad si hay que convertir a JPEG (90) |
| `--no-compress` | no comprimir antes de cifrar |
| `--no-verify` | no releer la imagen para comprobar la extracción |
| `--force` | sobrescribir la salida |
| `--json` | resultado en JSON |

```bat
icpu embed foto.png -o secreta.png -m "Nos vemos el jueves"
icpu embed foto.jpg -o secreta.jpg -f contrato.pdf             &rem JPEG: dominio DCT, sin recomprimir
icpu embed foto.png -o secreta.jpg -m "hola"                    &rem PNG → JPEG (calidad 90) y DCT
icpu embed anim.gif -o secreta.gif -f nota.txt                  &rem GIF animado: dominio de paleta
icpu embed foto.png -o secreta.png -f grande.zip --mode replace &rem más capacidad, más detectable
type informe.txt | icpu embed foto.png -o secreta.png --stdin --password-file clave.txt
```

Salida típica:

```text
Mensaje ocultado en secreta.png
  Formato / dominio : PNG / espacial
  Bits por ranura   : 1
  Mensaje           : 19 B
  Capacidad usada   : 0.18 % de 13.3 KiB
  Ranuras escritas  : 65536 (cambiaron 103)
  Verificacion      : correcta
  Tamano del fichero: 313.4 KiB -> 375.4 KiB
  Tiempo            : 0.515823 s
```

«Ranuras escritas» son las que STC consideró; «cambiaron» las que de verdad se modificaron: con
19 bytes, solo 103 de 460.800 muestras.

Si el mensaje **no cabe** en modo adaptativo, `embed` falla (código 5) y explica las alternativas:
una imagen más grande, comprimir el fichero, o `--mode replace`.

## `extract`: recuperar

```text
icpu extract <imagen> [-o SALIDA] [--layout L] [--force] [--json]
```

- Un **texto** se muestra en la consola. Las secuencias de escape del mensaje se neutralizan, para
  que un mensaje malicioso no pueda manipular el terminal.
- Un **fichero** se guarda con su nombre original en la carpeta actual o en la de `-o`. Los nombres
  hostiles se neutralizan: rutas, `C:x`, flujos NTFS (`x:y`), caracteres de control y dispositivos
  como `NUL`.

```bat
icpu extract secreta.png
icpu extract secreta.jpg -o C:\recuperados\
icpu extract secreta.png --json
```

```text
{"kind":"text","name":"","bytes":19,"text":"Nos vemos el jueves"}
```

Con la contraseña equivocada:

```text
Error (autenticacion): No se ha encontrado ningun mensaje: la contrasena es incorrecta, la imagen
no contiene datos ocultos o ha sido modificada.
```

y código de salida 6.

## `info`: capacidad

```text
icpu info <imagen> [--json]
```

```text
Imagen     : samples\paisaje.png
Formato    : PNG
Dimensiones: 480 x 320 px, 3 canal(es), 8 bits

Capacidad en modo adaptativo (por defecto, carga prudente):
  Salida  Dominio   Mensaje maximo
  PNG     espacial  13.3 KiB
  JPEG    dct       1.2 KiB

Capacidad maxima con --mode replace (mas detectable):
  Salida  Dominio   Bits/ranura  Ranuras utiles   Mensaje maximo
  PNG     espacial            1          460800   53.4 KiB
  ...
```

Para un JPEG también estima su calidad, si es progresivo y el submuestreo de croma. Las cifras son
para un mensaje **sin comprimir**: un texto suele comprimirse bastante.

## `analyze`: estegoanálisis

```text
icpu analyze <imagen> [--cover ORIGINAL] [--export DIR] [--steps N] [--json]
```

| Opción | Descripción |
| --- | --- |
| `--cover F` | la original, para medir MSE, PSNR, SSIM y muestras cambiadas |
| `--export DIR` | guarda planos de bits, mapa de diferencias, histograma y barrido (CSV) |
| `--steps N` | porciones del barrido chi-cuadrado (20) |

```text
Estegoanalisis:
  Chi-cuadrado (imagen completa): p = 0.0000  -> sin indicios
  Analisis RS: tasa estimada = 9.84 %  -> dudoso
  Barrido chi-cuadrado acumulado (p desde el principio hasta cada porcion):
      5%:0.00  10%:0.00  15%:0.00 ...
  Barrido chi-cuadrado por ventanas (p de cada porcion por separado):
      5%:0.00  10%:1.00  15%:0.00 ...
```

Cómo interpretarlo: [estegoanálisis](../tecnico/estegoanalisis.md).

## `bench`: experimentos

```text
icpu bench <portadora> [--fill L] [--bits L] [--modes L] [--layouts L] [--format EXT] [--csv F] [--json]
```

| Opción | Por defecto |
| --- | --- |
| `--fill` | `0.05,0.1,0.25,0.5,1` (fracciones de la capacidad de cada modo) |
| `--bits` | `1,2` |
| `--modes` | `adaptive,replace,match` |
| `--layouts` | `random,sequential` |

```bat
icpu bench foto.png --bits 1,2,3,4 --csv resultados.csv
icpu bench foto.png --format jpg --modes adaptive,replace --layouts random
```

Columnas del CSV: `bits, modo, orden, llenado, bytes, ranuras_cambiadas, psnr, ssim, chi2_p,
rs_tasa, dct_chi2_p, segundos, error` (−1 = no aplica en ese dominio). Ver la [guía de experimentos](experimentos.md).

## Códigos de salida

| Código | Significado |
| --- | --- |
| `0` | correcto |
| `2` | argumentos incorrectos |
| `3` | error de lectura o escritura |
| `4` | formato no soportado o fichero dañado |
| `5` | el mensaje no cabe |
| `6` | autenticación: contraseña incorrecta, sin mensaje o imagen modificada |
| `70` | error interno |

## Automatizar

```bat
@echo off
rem Oculta cada .txt de la carpeta "notas" en una copia de foto.png
for %%f in (notas\*.txt) do (
    icpu embed foto.png -o "salida\%%~nf.png" -f "%%f" --password-file clave.txt --force --json > "salida\%%~nf.json"
    if errorlevel 1 echo Fallo con %%f
)
```

```bash
#!/usr/bin/env bash
set -euo pipefail
icpu extract recibida.png --password-file ~/.icpu-clave --json | jq -r .text
```

Con `--json`, la salida es un único objeto JSON en la salida estándar, y los errores van a la de
error con el código de salida correspondiente.
