"""Genera las imagenes de ejemplo de samples/ (solo biblioteca estandar de Python).

    python scripts/generar_muestras.py

Dibuja un paisaje sintetico (cielo, sol, colinas y suelo con textura) con un
ruido leve, como el de un sensor real: sin ese ruido los bits bajos serian
demasiado regulares y el estegoanalisis no se pareceria al de una foto.
Escribe:
    samples/paisaje.png   portadora en color verdadero (PNG de 8 bits)
    samples/paisaje.gif   la misma escena con paleta de 252 colores (para el dominio de paleta)

Es determinista (semilla fija): siempre produce los mismos ficheros.
Las imagenes CON mensaje se generan despues con scripts/generar_demos.bat.
"""

import math
import random
import struct
import zlib
from pathlib import Path

W, H = 480, 320
OUT = Path(__file__).resolve().parent.parent / "samples"


def clamp(v):
    return 0 if v < 0 else 255 if v > 255 else int(v)


def scene():
    rnd = random.Random(2026)
    hills1 = [200 + 25 * math.sin(x / 47.0) + 12 * math.sin(x / 13.0 + 1) for x in range(W)]
    hills2 = [240 + 18 * math.sin(x / 71.0 + 2) + 8 * math.sin(x / 19.0) for x in range(W)]
    sun_x, sun_y, sun_r = 360, 90, 34
    rows = []
    for y in range(H):
        row = []
        for x in range(W):
            t = y / H
            r, g, b = 90 + 120 * t, 140 + 90 * t, 230 - 30 * t  # cielo en degradado
            d = math.hypot(x - sun_x, y - sun_y)
            if d < sun_r:
                r, g, b = 255, 236, 170
            elif d < sun_r * 2.2:  # halo
                k = 1 - (d - sun_r) / (sun_r * 1.2)
                r, g, b = r + (255 - r) * k * 0.6, g + (230 - g) * k * 0.5, b + (180 - b) * k * 0.3
            if y > hills1[x]:
                shade = 0.8 + 0.2 * math.sin(x / 9.0 + y / 7.0)
                r, g, b = 70 * shade, 120 * shade, 80 * shade
            if y > hills2[x]:
                tex = rnd.uniform(-14, 14)
                r, g, b = 110 + tex, 150 + tex + 30 * (y - hills2[x]) / 80, 70 + tex
            n = rnd.gauss(0, 2.2)  # ruido de "sensor"
            row.append((clamp(r + n), clamp(g + rnd.gauss(0, 2.2)), clamp(b + rnd.gauss(0, 2.2))))
        rows.append(row)
    return rows


def write_png(path, rows):
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)
    text = b"Comment\x00Imagen de ejemplo sintetica de I-CPU"
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"tEXt", text) +
                     chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def write_gif(path, rows):
    # Paleta: cubo RGB 6x7x6 = 252 colores (+4 sin usar hasta 256).
    levels_r, levels_g, levels_b = 6, 7, 6
    palette = bytearray()
    for ri in range(levels_r):
        for gi in range(levels_g):
            for bi in range(levels_b):
                palette += bytes((round(ri * 255 / 5), round(gi * 255 / 6), round(bi * 255 / 5)))
    palette += bytes(3 * (256 - levels_r * levels_g * levels_b))

    indices = []
    for row in rows:
        for r, g, b in row:
            ri, gi, bi = round(r * 5 / 255), round(g * 6 / 255), round(b * 5 / 255)
            indices.append((ri * levels_g + gi) * levels_b + bi)

    # LZW minimo y valido: CLEAR cada 254 codigos, asi el ancho se queda en 9 bits.
    codes = []
    for i, v in enumerate(indices):
        if i % 254 == 0:
            codes.append(256)
        codes.append(v)
    codes.append(257)
    acc = nbits = 0
    data = bytearray()
    for c in codes:
        acc |= c << nbits
        nbits += 9
        while nbits >= 8:
            data.append(acc & 255)
            acc >>= 8
            nbits -= 8
    if nbits:
        data.append(acc & 255)

    out = bytearray(b"GIF89a") + struct.pack("<HHBBB", W, H, 0xF7, 0, 0) + palette
    out += b"\x2c" + struct.pack("<HHHHB", 0, 0, W, H, 0) + b"\x08"
    for i in range(0, len(data), 255):
        block = data[i:i + 255]
        out += bytes((len(block),)) + block
    out += b"\x00\x3b"
    path.write_bytes(bytes(out))


def main():
    OUT.mkdir(exist_ok=True)
    rows = scene()
    write_png(OUT / "paisaje.png", rows)
    write_gif(OUT / "paisaje.gif", rows)
    print(f"Generadas {OUT / 'paisaje.png'} y {OUT / 'paisaje.gif'}")


if __name__ == "__main__":
    main()
