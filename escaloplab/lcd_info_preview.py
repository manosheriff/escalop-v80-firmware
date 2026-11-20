"""Render the Escaloplab Info screen cards (menu_info.cpp) as one PNG, from the glyph data in the source.

usage: python escaloplab/lcd_info_preview.py <out.png>   (run from the Marlin Escaloplab folder)
"""
import re, sys
from PIL import Image, ImageDraw, ImageFont

info = open('Marlin/src/lcd/menu/menu_info.cpp', encoding='utf-8').read()
hd = open('Marlin/src/lcd/HD44780/marlinui_HD44780.cpp', encoding='utf-8').read()
out = sys.argv[1]

def arr(src, name):
    m = re.search(name + r'(?:\[\d*\])+\s*=\s*\{(.*?)\};', src, re.S)
    rows = [int(b, 2) for b in re.findall(r'B([01]{5})', m.group(1))]
    return [rows[i:i + 8] for i in range(0, len(rows), 8)]

G = arr(info.split('namespace EscInfo')[1], 'glyphs')          # 0 logo 1 deg 2 on 3 off 4 noz 5 bed 6 clock 7 warn
font = arr(hd, 'esc_font')

PX, GAP = 6, 1
CW, CH = 5 * (PX + GAP), 8 * (PX + GAP)
CGAP = PX + GAP
BG, OFF, ON = (28, 60, 200), (38, 72, 214), (235, 245, 255)
MARGIN = 18
W = MARGIN * 2 + 20 * CW + 19 * CGAP
H = MARGIN * 2 + 4 * CH + 3 * CGAP
tfont = ImageFont.truetype('consolab.ttf', int(CH * 0.92))
lfont = ImageFont.truetype('consolab.ttf', 26)

def screen(cells):
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
            if txt:
                d.text((x0 + CW // 2, y0 + CH // 2 - 2), txt, font=tfont, fill=ON, anchor='mm')
    return im

def text(cells, col, row, s):
    for i, ch in enumerate(s):
        cells[(col + i, row)] = G[1] if ch == '°' else ch

PAGES = 6   # cover, live, machine, steps, wi-fi, firmware

def card(page, title, rows):
    c = {(0, 0): G[0]}
    text(c, 2, 0, title)
    for i in range(PAGES): c[(20 - PAGES + i, 0)] = G[2 if i == page else 3]
    for r, (icon, label, value) in enumerate(rows, 1):
        if icon is not None: c[(0, r)] = G[icon]
        text(c, 2, r, label); text(c, 20 - len(value), r, value)
    return c

# Cover
top = [0,2, 7,2, 0,2, 0,1, 6,None, 0,1, 0,1, 6,None, 0,1, 0,1]
bot = [7,5, 5,4, 3,5, 0,1, 3,5, 3,4, 0,2, 3,5, 0,1, 7,4]
cover = {}
text(cover, 4, 0, '3D PRINTERS')
for i in range(20):
    if top[i] is not None: cover[(i, 1)] = font[top[i]]
    cover[(i, 2)] = font[bot[i]]
text(cover, 3, 3, 'escaloplab.com'); cover[(19, 3)] = '>'

live = card(1, 'LIVE', [(4, 'Hotend', '205/210°'), (5, 'Bed', '60/60°'), (6, 'Powered on', "12'34")])
fault = card(1, 'LIVE', [(4, 'Hotend', '23° off'), (7, 'Bed', 'NO SENSOR'), (6, 'Powered on', "03'10")])
machine = card(2, 'MACHINE', [(None, 'Build', '210x210x200'), (None, 'Hotend', 'E3D V6'), (None, 'Bed', 'MK2B + PEI')])
motion = card(3, 'STEPS / MM', [])
text(motion, 2, 1, 'X'); text(motion, 5, 1, '80.0');  text(motion, 12, 1, 'Y'); text(motion, 16, 1, '80.0')
text(motion, 2, 2, 'Z'); text(motion, 4, 2, '800.0'); text(motion, 12, 2, 'E'); text(motion, 15, 2, '186.0')
text(motion, 2, 3, 'XY 1/16   Z+E 1/32')
wifi = card(4, 'WI-FI', [(None, 'Wi-Fi', 'Connected'), (None, 'IP', '192.168.1.57'), (None, 'Serial', '00123')])
firmware = card(5, 'FIRMWARE', [(None, 'Escaloplab', 'v1.7.0'), (None, 'Marlin', '2.1.2.9'), (None, 'Board', 'RAMPS 1.4')])

shots = [('1  Cover', cover), ('2  Live', live), ('2  Live - bed sensor missing (icon blinks)', fault),
         ('3  Machine', machine), ('4  Steps / mm (read live)', motion), ('5  Wi-Fi', wifi), ('6  Firmware', firmware)]
LH = 40
sheet = Image.new('RGB', (W * 2 + 30, (H + LH) * 4 + 20), (245, 245, 245))
d = ImageDraw.Draw(sheet)
for i, (label, cells) in enumerate(shots):
    x = 10 + (i % 2) * (W + 10); y = 10 + (i // 2) * (H + LH)
    d.text((x + 4, y + 6), label, font=lfont, fill=(30, 30, 30))
    sheet.paste(screen(cells), (x, y + LH))
sheet.save(out)
print(out)
