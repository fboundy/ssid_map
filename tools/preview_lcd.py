# Offline preview of the LCD layout: parses src/fonts.h and mirrors render()
# from src/display.cpp so the geometry can be eyeballed without hardware.
import re, sys
from PIL import Image, ImageDraw

W, H = 320, 170
MID_Y, RIGHT_W = 88, 104
RIGHT_X = W - RIGHT_W
LEFT_X = 6
LEFT_W = RIGHT_X - LEFT_X - 6

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

def fit(s, w):
    if textw(XL, s) <= w: return XL
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
    cx = W // 2; rcx = RIGHT_X + RIGHT_W // 2
    if override:
        centre(px, cx, 12, SMALL, C_BAD, "Over-ridden by")
        f = fit(override, W - 12)
        centre(px, cx, 34, f, C_FG, override, (W - 12) // f["w"])
    else:
        text(px, LEFT_X, 8, SMALL, C_DIM, "Connected to:")
        s = ssid if connected else "Not connected"
        f = fit(s, LEFT_W)
        text(px, LEFT_X, 32, f, C_FG if connected else C_DIM, s, LEFT_W // f["w"])
        online = connected and nat
        if online and valid:
            for y, dn, v, col in ((10, True, down, C_OK), (48, False, up, C_ACC)):
                val = "%.1f" % v
                x = RIGHT_X + 6
                arrow(d, x, y, 14, 22, dn, col)
                text(px, x + 14 + 5, y - 3, LARGE, C_FG, val)
        elif online and running:
            centre(px, rcx, 30, SMALL, C_WARN, "Speed")
            centre(px, rcx, 50, SMALL, C_WARN, "test...")
        else:
            no_entry(px, rcx - 14, 6, 28, C_BAD)
            centre(px, rcx, 40, SMALL, C_BAD, "No")
            centre(px, rcx, 60, SMALL, C_BAD, "Internet")
    d.rectangle([0, MID_Y, W - 1, MID_Y], fill=C_LINE)
    if not host:
        centre(px, cx, 120, SMALL, C_DIM, "No named host set")
    else:
        line = host + (" Connected" if host_here else " Not Connected")
        hf = fit(line, W - 12)
        y = 98 if host_here else 110
        centre(px, cx, y, hf, C_OK if host_here else C_DIM, line, (W - 12) // hf["w"])
        if host_here and y + hf["h"] + 2 < H - SMALL["h"]:
            centre(px, cx, y + hf["h"] + 2, SMALL, C_DIM, "192.168.137.11")
    return img

cases = [
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
