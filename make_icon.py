#!/usr/bin/env python3
"""make_icon.py — build SkyRoads.icns from the ship sprite.

Input: 29x24 RGBA dump from cars_dump.  Output: .icns via iconutil.
Stdlib only.  The icon is macOS-style: rounded dark space tile, starfield,
ship pixel art scaled with nearest neighbor.
"""
import struct, sys, zlib, os, subprocess, random, tempfile

CARW, CARH = 29, 24
S = 1024                      # master size
CORNER = 232                  # macOS icon-grid corner radius at 1024


def write_png(path, w, h, rgba):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4])
                   for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def rounded_alpha(x, y):
    rx = min(x, S - 1 - x)
    ry = min(y, S - 1 - y)
    if rx >= CORNER or ry >= CORNER:
        return 255
    dx, dy = CORNER - rx, CORNER - ry
    d = (dx * dx + dy * dy) ** 0.5
    if d <= CORNER - 1:
        return 255
    if d >= CORNER + 1:
        return 0
    return int(255 * (CORNER + 1 - d) / 2)


def load_image_1024(path, td):
    """Any image sips can read -> (S,S) RGB rows via BMP round-trip."""
    bmp = os.path.join(td, "icon.bmp")
    subprocess.run(["sips", "-s", "format", "bmp", "-z", str(S), str(S),
                    path, "--out", bmp], check=True, capture_output=True)
    d = open(bmp, "rb").read()
    off = struct.unpack("<I", d[10:14])[0]
    w, h = struct.unpack("<ii", d[18:26])
    bpp = struct.unpack("<H", d[28:30])[0]
    assert (w, abs(h), bpp) == (S, S, 24) or (w, abs(h), bpp) == (S, S, 32), "unexpected BMP"
    stride = ((w * (bpp // 8) + 3) // 4) * 4
    px = bytearray(S * S * 3)
    for y in range(S):
        src_y = (S - 1 - y) if h > 0 else y      # BMP bottom-up
        row = off + src_y * stride
        for x in range(S):
            b, g, r = d[row + x * (bpp // 8):row + x * (bpp // 8) + 3]
            i = (y * S + x) * 3
            px[i:i + 3] = bytes((r, g, b))
    return px


def main_from_image(path, out_icns):
    with tempfile.TemporaryDirectory() as td:
        px = load_image_1024(path, td)
        img = bytearray(S * S * 4)
        for y in range(S):
            for x in range(S):
                a = rounded_alpha(x, y)
                i3, i4 = (y * S + x) * 3, (y * S + x) * 4
                img[i4:i4 + 4] = bytes((px[i3], px[i3 + 1], px[i3 + 2], a))
        emit_iconset(img, out_icns, td)


def emit_iconset(img, out_icns, td):
    iconset = os.path.join(td, "SkyRoads.iconset")
    os.mkdir(iconset)
    write_png(os.path.join(iconset, "icon_512x512@2x.png"), S, S, img)
    for sz in (512, 256, 128, 64, 32, 16):
        subprocess.run(["sips", "-z", str(sz), str(sz),
                        os.path.join(iconset, "icon_512x512@2x.png"),
                        "--out", os.path.join(iconset, f"icon_{sz}x{sz}.png")],
                       check=True, capture_output=True)
    subprocess.run(["iconutil", "-c", "icns", iconset, "-o", out_icns],
                   check=True)


def main(ship_bin, out_icns):
    ship = open(ship_bin, "rb").read()
    assert len(ship) == CARW * CARH * 4, "bad ship dump"

    img = bytearray(S * S * 4)
    rnd = random.Random(1993)                     # deterministic starfield
    stars = [(rnd.randrange(40, S - 40), rnd.randrange(40, S - 40),
              rnd.choice([120, 170, 220])) for _ in range(90)]
    star_at = {}
    for sx, sy, b in stars:
        star_at[(sx, sy)] = b
        if b > 150:                               # brighter stars get a cross
            for d in (-1, 1):
                star_at.setdefault((sx + d, sy), b // 2)
                star_at.setdefault((sx, sy + d), b // 2)

    # ship placement: nearest-neighbor scale, centered
    scale = 28                                    # 29*28 = 812 px wide
    shw, shh = CARW * scale, CARH * scale
    ox, oy = (S - shw) // 2, (S - shh) // 2

    for y in range(S):
        for x in range(S):
            a = rounded_alpha(x, y)
            i = (y * S + x) * 4
            if a == 0:
                continue
            # vertical space gradient: deep navy -> near black
            t = y / S
            r, g, b = int(14 + 10 * (1 - t)), int(14 + 8 * (1 - t)), int(40 + 26 * (1 - t))
            sb = star_at.get((x, y))
            if sb:
                r, g, b = sb, sb, min(255, sb + 30)
            # ship overlay
            if ox <= x < ox + shw and oy <= y < oy + shh:
                sx, sy = (x - ox) // scale, (y - oy) // scale
                si = (sy * CARW + sx) * 4
                if ship[si + 3]:
                    r, g, b = ship[si], ship[si + 1], ship[si + 2]
            img[i:i + 4] = bytes((r, g, b, a))

    with tempfile.TemporaryDirectory() as td:
        emit_iconset(img, out_icns, td)


if __name__ == "__main__":
    if sys.argv[1].endswith(".bin"):
        main(sys.argv[1], sys.argv[2])
    else:
        main_from_image(sys.argv[1], sys.argv[2])
