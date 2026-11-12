"""Render a preview of the Escaloplab LCD screens from the glyph data in the firmware source."""
import re, sys
from PIL import Image, ImageDraw, ImageFont

src = open(sys.argv[1], encoding='utf-8').read()
out = sys.argv[2]

def arr(name):
    m = re.search(name + r'(?:\[\d*\])+\s*=\s*\{(.*?)\};', src, re.S)
    rows = [int(b, 2) for b in re.findall(r'B([01]{5})', m.group(1))]
    return [rows[i:i + 8] for i in range(0, len(rows), 8)]

logo, font = arr('esc_logo'), arr('esc_font')
noz, bed, fan = arr('esc_ico_nozzle'), arr('esc_ico_bed'), arr('esc_ico_fan')
speed, bar_e, bar_f = arr('esc_ico_speed')[0], arr('esc_bar_empty')[0], arr('esc_bar_full')[0]
degree = [0b01100, 0b10010, 0b10010, 0b01100, 0, 0, 0, 0]
clock = [0, 0b01110, 0b10011, 0b10101, 0b10001, 0b01110, 0, 0]

PX, GAP = 6, 1                 # size of one LCD dot / gap between dots
CW, CH = 5 * (PX + GAP), 8 * (PX + GAP)
CGAP = PX + GAP                # gap between character cells (about one dot)
BG, OFF, ON = (28, 60, 200), (38, 72, 214), (235, 245, 255)   # blue LCD, white dots
MARGIN = 18
W = MARGIN * 2 + 20 * CW + 19 * CGAP
H = MARGIN * 2 + 4 * CH + 3 * CGAP
tfont = ImageFont.truetype('consolab.ttf', int(CH * 0.92))

def screen(cells):
    """cells: dict (col,row) -> glyph (list of 8 ints) or a 1-char string"""
    im = Image.new('RGB', (W, H), BG)
    d = ImageDraw.Draw(im)
    for r in range(4):
        for c in range(20):
            x0 = MARGIN + c * (CW + CGAP); y0 = MARGIN + r * (CH + CGAP)
            g = cells.get((c, r))
            txt = g if isinstance(g, str) and g != ' ' else None
            if not isinstance(g, list): g = [0] * 8
            for y in range(8):
                for x in range(5):
                    on = g[y] >> (4 - x) & 1
                    px = x0 + x * (PX + GAP); py = y0 + y * (PX + GAP)
                    d.rectangle([px, py, px + PX - 1, py + PX - 1], fill=ON if on else OFF)
            if txt:  # built-in LCD font: drawn with a system font (approximation)
                d.text((x0 + CW // 2, y0 + CH // 2 - 2), txt, font=tfont, fill=ON, anchor='mm')
    return im

def text(cells, col, row, s, sub=None):
    for i, ch in enumerate(s):
        cells[(col + i, row)] = (sub or {}).get(ch, ch)

# --- Scene 1: logo ---
c1 = {}
for k in range(3):
    for i in (1, 2, 3): c1[(7 + i, k)] = logo[i]
    c1[(7, k + 1)] = logo[0]; c1[(11, k + 1)] = logo[4]

# --- Scene 2: wordmark ---
top = [0,2, 7,2, 0,2, 0,1, 6,None, 0,1, 0,1, 6,None, 0,1, 0,1]
bot = [7,5, 5,4, 3,5, 0,1, 3,5, 3,4, 0,2, 3,5, 0,1, 7,4]
c2 = {}
for i in range(20):
    if top[i] is not None: c2[(i, 1)] = font[top[i]]
    c2[(i, 2)] = font[bot[i]]
text(c2, 4, 0, '3D PRINTERS'); text(c2, 3, 3, 'escaloplab.com')

# --- Status: idle (heating) ---
ic = {'n': noz[1], 'b': bed[1], 'd': degree, 'f': fan[0], 's': speed, 'c': clock}
c3 = {}
text(c3, 0, 0, 'n142/205d', ic); text(c3, 10, 0, 'b 58/60d', ic)
text(c3, 0, 1, 'X 105 Y 105 Z  10.00')
text(c3, 0, 2, 'f  0%  s100%  c--:--', ic)
text(c3, 0, 3, 'Escaloplab Ready.')

# --- Status: printing 48% ---
ic2 = {'n': noz[0], 'b': bed[1], 'd': degree, 'f': fan[1], 'c': clock}
c4 = {}
text(c4, 0, 0, 'n205/205d', ic2); text(c4, 10, 0, 'b 60/60d', ic2)
px = 48 * 75 // 100; full, rem = px // 5, px % 5
fill = 0x1F & ~(0x1F >> rem)
edge = [bar_e[i] | (fill if 1 <= i <= 6 else 0) for i in range(8)]
for i in range(15): c4[(i, 1)] = bar_f if i < full else (edge if (i == full and rem) else bar_e)
text(c4, 16, 1, ' 48%')
text(c4, 0, 2, 'f100% Z 12.40 c01:23', ic2)
text(c4, 0, 3, 'benchy.gcode')

panels = [('Boot 1: logo (animated, chevrons light bottom to top)', c1), ('Boot 2: wordmark', c2),
          ('Status: idle, heating up', c3), ('Status: printing', c4)]
lab = ImageFont.truetype('segoeui.ttf', 24)
sheet = Image.new('RGB', (W * 2 + 60, (H + 60) * 2 + 20), (17, 18, 23))
d = ImageDraw.Draw(sheet)
for i, (title, cells) in enumerate(panels):
    x = 20 + (i % 2) * (W + 20); y = 20 + (i // 2) * (H + 60)
    d.text((x, y), title, font=lab, fill=(255, 46, 138))
    sheet.paste(screen(cells), (x, y + 38))
sheet.save(out)
print('saved', out, sheet.size)

# ---------- animated boot GIF ----------
frames, durs = [], []
def add(cells, ms): frames.append(screen(dict(cells))); durs.append(ms)
def masked(g, m): return [r & m for r in g]
GL = [1, 3, 7, 15, 31]; GR = [16, 24, 28, 30, 31]
cur = {}
add(cur, 350)
for k in (2, 1, 0):
    for m in (0b00100, 0b01110, 0b11111):
        cur[(9, k)] = masked(logo[2], m); add(cur, 40)
    for i in range(5):
        cur[(8, k)] = masked(logo[1], GL[i]); cur[(10, k)] = masked(logo[3], GR[i]); add(cur, 40)
    for i in range(5):
        cur[(7, k + 1)] = masked(logo[0], GL[i]); cur[(11, k + 1)] = masked(logo[4], GR[i]); add(cur, 40)
    add(cur, 170)
add(cur, 250); add({}, 70); add(cur, 130); add({}, 50); add(cur, 1500)
for y in (-1, -2, -3, -4):
    add({(c, r + y): g for (c, r), g in cur.items() if r + y >= 0}, 95)
add({}, 200)
cur = {}
for i in range(20):
    if top[i] is not None: cur[(i, 1)] = font[top[i]]
    cur[(i, 2)] = font[bot[i]]; add(cur, 45)
add(cur, 300)
nolab = {k: v for k, v in cur.items() if k[0] < 14}
add(nolab, 70); add(cur, 120); add(nolab, 50); add(cur, 250)
for i in range(11): text(cur, 4, 0, '3D PRINTERS'[:i + 1]); add(cur, 40)
text(cur, 3, 3, 'escaloplab.com'); add(cur, 1500)
for c in range(20): cur.pop((c, 3), None)
text(cur, 3, 3, 'FIRMWARE V1.1'); add(cur, 1100)
for c in range(20): cur.pop((c, 3), None)
text(cur, 1, 3, 'Powered by Marlin'); add(cur, 1200)
small = [f.resize((W // 2, H // 2), Image.LANCZOS) for f in frames]
gif = out.replace('.png', '-boot.gif')
small[0].save(gif, save_all=True, append_images=small[1:], duration=durs, loop=0)
print('saved', gif, len(frames), 'frames', sum(durs), 'ms')
