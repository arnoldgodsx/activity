"""Uygulama ve tepsi simgelerini (.ico) harici kütüphane olmadan üretir."""
import math
import struct
import sys

SIZES = (16, 20, 24, 32, 48, 64)
COLORS = {
    "app": (37, 99, 235),   # mavi
    "on": (22, 163, 74),    # yeşil
    "off": (220, 38, 38),   # kırmızı
}


def coverage(x, y):
    """(x, y) birim koordinatta (-1..1) güç simgesi rengi: 0 yok, 1 disk, 2 beyaz."""
    r = math.hypot(x, y)
    if r > 0.96:
        return 0
    # dikey çubuk
    if abs(x) <= 0.09 and -0.66 <= y <= -0.02:
        return 2
    # üstte boşluk bırakan halka
    if 0.40 <= r <= 0.56:
        ang = math.degrees(math.atan2(x, -y))  # 0 = yukarı
        if abs(ang) > 38:
            return 2
    return 1


def render(size, color, ss=4):
    rows = []
    for py in range(size):
        row = []
        for px in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    x = ((px + (sx + 0.5) / ss) / size) * 2 - 1
                    y = ((py + (sy + 0.5) / ss) / size) * 2 - 1
                    c = coverage(x, y)
                    if c == 0:
                        continue
                    rgb = (255, 255, 255) if c == 2 else color
                    acc[0] += rgb[0]
                    acc[1] += rgb[1]
                    acc[2] += rgb[2]
                    acc[3] += 1
            n = ss * ss
            a = acc[3] / n
            if acc[3]:
                rgb = [v / acc[3] for v in acc[:3]]
            else:
                rgb = [0, 0, 0]
            row.append((round(rgb[0]), round(rgb[1]), round(rgb[2]), round(a * 255)))
        rows.append(row)
    return rows


def ico_image(size, color):
    pixels = render(size, color)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    xor = bytearray()
    for row in reversed(pixels):  # BMP alttan üste
        for r, g, b, a in row:
            xor += bytes((b, g, r, a))
    mask_row = ((size + 31) // 32) * 4
    and_mask = bytes(mask_row * size)
    return header + bytes(xor) + and_mask


def write_ico(path, color):
    images = [ico_image(s, color) for s in SIZES]
    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for s, data in zip(SIZES, images):
        out += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for data in images:
        out += data
    with open(path, "wb") as f:
        f.write(out)


if __name__ == "__main__":
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."
    for name, color in COLORS.items():
        write_ico(f"{outdir}/{name}.ico", color)
