# Offline preview of the LCD layout: parses src/fonts.h and mirrors render()
# from src/display.cpp so the geometry can be eyeballed without hardware.
import re, sys
from PIL import Image, ImageDraw

W, H = 320, 170
MID_Y, RIGHT_W, BOT_Y = 88, 96, 90
RIGHT_X = W - RIGHT_W
LEFT_X = 5
LEFT_W = RIGHT_X - LEFT_X - 5

src = open(r"C:\Users\FrancisBoundy\Documents\GitHub\ssid_map\src\fonts.h").read()

def load(name):
    m = re.search(r"static const Font %s = \{ (\d+), (\d+), (\d+)," % name, src)
    w, h, rb = int(m.group(1)), int(m.group(2)), int(m.group(3))
    block = src.split("static const uint8_t %s_DATA" % name)[1].split("};")[0]
    glyphs = []
    for line in block.splitlines():
        if "/*" not in line: continue
        glyphs.append([int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", line)])
    return {"w": w, "h": h, "rb": rb, "g": glyphs}

SMALL, MED, LARGE, XL = load("FONT_SMALL"), load("FONT_MED"), load("FONT_LARGE"), load("FONT_XL")

C_BG=(0,0,0); C_FG=(235,235,235); C_DIM=(140,150,165); C_LINE=(60,70,85)
C_OK=(60,200,110); C_WARN=(240,180,60); C_BAD=(240,80,80); C_ACC=(90,170,255)

def textw(f, s): return len(s) * f["w"]

def fit(s, w, allow_xl=True):
    if allow_xl and textw(XL, s) <= w: return XL
    if textw(LARGE, s) <= w: return LARGE
    if textw(MED, s) <= w: return MED
    return SMALL

def text(px, x, y, f, col, s, maxc=0):
    n = 0
    for ch in s:
        if maxc and n >= maxc: break
        o = ord(ch)
        if o < 32 or o > 126: o = ord('?')
        g = f["g"][o - 32]
        for j in range(f["h"]):
            for i in range(f["w"]):
                if g[j * f["rb"] + (i >> 3)] & (0x80 >> (i & 7)):
                    xx, yy = x + i, y + j
                    if 0 <= xx < W and 0 <= yy < H: px[xx, yy] = col
        x += f["w"]; n += 1

def centre(px, cx, y, f, col, s, maxc=0):
    t = s[:maxc] if (maxc and len(s) > maxc) else s
    text(px, cx - textw(f, t) // 2, y, f, col, t)

def arrow(d, x, y, w, h, down, col):
    cx = x + w // 2; head = h // 2; stem = w // 3
    if down:
        d.rectangle([cx - stem // 2, y, cx - stem // 2 + stem - 1, y + h - head - 1], fill=col)
        base = y + h - head
        for j in range(head):
            half = (w // 2) - (w // 2) * j // head
            d.rectangle([cx - half, base + j, cx + half, base + j], fill=col)
    else:
        d.rectangle([cx - stem // 2, y + head, cx - stem // 2 + stem - 1, y + h - 1], fill=col)
        base = y + head
        for j in range(head):
            half = (w // 2) - (w // 2) * j // head
            d.rectangle([cx - half, base - j, cx + half, base - j], fill=col)

def thick_line(d, x0, y0, x1, y1, t, col):
    dx = abs(x1 - x0); sx = 1 if x0 < x1 else -1
    dy = -abs(y1 - y0); sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        d.rectangle([x0 - t // 2, y0 - t // 2, x0 - t // 2 + t - 1, y0 - t // 2 + t - 1], fill=col)
        if x0 == x1 and y0 == y1: break
        e2 = 2 * err
        if e2 >= dy: err += dy; x0 += sx
        if e2 <= dx: err += dx; y0 += sy

def tick(d, x, y, dia, col):
    t = dia // 7 + 2
    thick_line(d, x + dia // 8, y + dia // 2, x + dia * 2 // 5, y + dia * 4 // 5, t, col)
    thick_line(d, x + dia * 2 // 5, y + dia * 4 // 5, x + dia * 7 // 8, y + dia // 6, t, col)

def no_entry(px, x, y, dia, col):
    r = dia // 2; cx, cy = x + r, y + r; inner = (r - 3) ** 2
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            rr = dx * dx + dy * dy
            if inner <= rr <= r * r and 0 <= cx + dx < W and 0 <= cy + dy < H:
                px[cx + dx, cy + dy] = col
    for i in range(-r + 3, r - 2):
        for t in (-1, 0, 1):
            if 0 <= cx + i < W and 0 <= cy + i + t < H: px[cx + i, cy + i + t] = col

def render(ssid, connected, nat, down, up, valid, running, override, host, host_here):
    img = Image.new("RGB", (W, H), C_BG); px = img.load(); d = ImageDraw.Draw(img)
    rcx = RIGHT_X + RIGHT_W // 2
    if override:
        text(px, LEFT_X, 8, SMALL, C_BAD, "Over-ridden by:")
        f = fit(override, W - 12)
        text(px, LEFT_X, 32, f, C_FG, override, (W - 12) // f["w"])
    else:
        text(px, LEFT_X, 8, SMALL, C_DIM, "Internet Connection:")
        s = ssid if connected else "Not connected"
        f = fit(s, LEFT_W)
        text(px, LEFT_X, 32, f, C_FG if connected else C_DIM, s, LEFT_W // f["w"])
        online = connected and nat
        if online and valid:
            for y, dn, v, col in ((8, True, down, C_OK), (38, False, up, C_ACC)):
                x = RIGHT_X + 4
                arrow(d, x, y, 14, 22, dn, col)
                text(px, x + 19, y - 3, LARGE, C_FG, "%.1f" % v)
            centre(px, rcx, 68, SMALL, C_DIM, "Mb/s")
        elif online and running:
            centre(px, rcx, 30, SMALL, C_WARN, "Speed")
            centre(px, rcx, 50, SMALL, C_WARN, "test...")
        else:
            no_entry(px, rcx - 14, 6, 28, C_BAD)
            centre(px, rcx, 40, SMALL, C_BAD, "No")
            centre(px, rcx, 60, SMALL, C_BAD, "Internet")
    d.rectangle([0, MID_Y, W - 1, MID_Y], fill=C_LINE)

    text(px, LEFT_X, BOT_Y + 6, SMALL, C_DIM, "Device Connection:")
    if not host:
        text(px, LEFT_X, BOT_Y + 26, fit("None set", LEFT_W, False), C_DIM, "None set")
        return img
    hf = fit(host, LEFT_W, False)
    text(px, LEFT_X, BOT_Y + 26, hf, C_FG if host_here else C_DIM, host, LEFT_W // hf["w"])
    if host_here:
        text(px, LEFT_X, BOT_Y + 56, SMALL, C_DIM, "192.168.137.11")
    if host_here:
        tick(d, rcx - 14, BOT_Y + 6, 28, C_OK)
        centre(px, rcx, BOT_Y + 40, SMALL, C_OK, "Connected")
    else:
        no_entry(px, rcx - 14, BOT_Y + 6, 28, C_BAD)
        centre(px, rcx, BOT_Y + 40, SMALL, C_BAD, "Not")
        centre(px, rcx, BOT_Y + 58, SMALL, C_BAD, "Connected")
    return img

cases = [
    ("long-16ch",      dict(ssid="Vehicle Mobile 1", connected=1, nat=1, down=23.9, up=18.5, valid=1, running=0, override="", host="Vehicle Mobile 1", host_here=1)),
    ("routing-short",  dict(ssid="Dlink12", connected=1, nat=1, down=23.9, up=18.5, valid=1, running=0, override="", host="Laptop", host_here=1)),
    ("routing-long",   dict(ssid="ToppinFibrus", connected=1, nat=1, down=123.4, up=9.8, valid=1, running=0, override="", host="Francis Laptop", host_here=1)),
    ("no-internet",    dict(ssid="ToppinHouseGuest", connected=1, nat=0, down=0, up=0, valid=0, running=0, override="", host="Laptop", host_here=0)),
    ("override",       dict(ssid="", connected=0, nat=0, down=0, up=0, valid=0, running=0, override="ToppinFibrus", host="Laptop", host_here=0)),
    ("no-host",        dict(ssid="Dlink12", connected=1, nat=1, down=23.9, up=18.5, valid=1, running=1, override="", host="", host_here=0)),
]
sheet = Image.new("RGB", (W + 2, (H + 6) * len(cases)), (30, 30, 30))
for i, (name, kw) in enumerate(cases):
    sheet.paste(render(**kw), (1, i * (H + 6) + 3))
out = sys.argv[1] if len(sys.argv) > 1 else "preview.png"
sheet.save(out)
print("wrote", out, "cases:", ", ".join(n for n, _ in cases))
