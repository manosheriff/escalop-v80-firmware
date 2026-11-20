"""Render the main Escaloplab Wi-Fi pages (menu_escaloplab_wifi.cpp) as one PNG.

usage: python escaloplab/lcd_wifi_preview.py <out.png>   (run from the Marlin Escaloplab folder)
"""
import re, sys
from PIL import Image, ImageDraw, ImageFont

src = open('Marlin/src/lcd/menu/menu_escaloplab_wifi.cpp', encoding='utf-8').read()
out = sys.argv[1]

m = re.search(r'pw_glyphs\[PG_COUNT\]\[8\]\s*=\s*\{(.*?)\};', src, re.S)
rows = [int(b, 2) for b in re.findall(r'B([01]{5})', m.group(1))]
G = [rows[i:i + 8] for i in range(0, len(rows), 8)]   # 0 space 1 del 2 done 3 cancel 4 lock 5 '\' 6 '~'

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

def page(lines, center=True):
    c = {}
    for r, l in enumerate(lines):
        if isinstance(l, str):
            col = (20 - len(l)) // 2 if center else 0
            for i, ch in enumerate(l): c[(col + i, r)] = ch
        else:
            for i, cell in enumerate(l): c[(i, r)] = cell
    return c

# Password editor: typed "hunter2", the strip on 'A'
items = [chr(ord('a') + i) for i in range(26)] + [chr(ord('A') + i) for i in range(26)] + [str(i) for i in range(10)] \
      + [' '] + list('!"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~') + ['DEL', 'DONE', 'CANCEL']
def cell(it):
    return {' ': G[0], 'DEL': G[1], 'DONE': G[2], 'CANCEL': G[3], '\\': G[5], '~': G[6]}.get(it, it)
pick = items.index('A')
strip = []
for c in range(20):
    off = c - 9 if c < 9 else 0 if c == 10 else c - 11
    strip.append('[' if c == 9 else ']' if c == 11 else cell(items[(pick + off) % len(items)]))
pw = page([[G[4], ' '] + list('MyWiFi 5G') + [' '] * 9, list('hunter2_') + [' '] * 12, strip, list('Add: A') + [' '] * 10 + list('7/63')])
pick = items.index('DONE')
strip2 = []
for c in range(20):
    off = c - 9 if c < 9 else 0 if c == 10 else c - 11
    strip2.append('[' if c == 9 else ']' if c == 11 else cell(items[(pick + off) % len(items)]))
pw2 = page([[G[4], ' '] + list('MyWiFi 5G') + [' '] * 9, list('hunter2_') + [' '] * 12, strip2, list('Done: connect') + [' '] * 3 + list('7/63')])

shots = [
    ('Start-up: setup hotspot', page(['Wi-Fi setup', 'On your phone join:', 'Escalop-v80-00123', 'Or: Menu > Wi-Fi'])),
    ('Wi-Fi menu', page([' Main', ' Connected: MyWiFi 5', ' IP: 192.168.1.57', '>Choose network'], center=False)),
    ('Network list (lock = password)', page([' Back', '>MyWiFi 5G ' , ' Guest', ' Neighbour with a l'], center=False)),
    ('Password: turn the knob, click adds', pw),
    ('Password: the actions at the end', pw2),
    ('Connecting', page(['', 'Connecting to', 'MyWiFi 5G', '...'])),
    ('Wrong password', page(['', 'Wrong Wi-Fi password', '', '[Try again]  Cancel '], center=False)),
    ('Firmware offer (status screen)', page(['New firmware 1.7.1', 'is available.', 'Installs over Wi-Fi.', '[Update]      Later '])),
    ('Receiving a print over Wi-Fi', page(['Receiving over Wi-Fi', 'BENCHY.GCO', '[========          ]', '42%   880 KB'])),
    ('Update from SD card', page(['Sending firmware', 'EscalopV80_Firmware_', '[==============    ]', '78%   164 KB'])),
]
# lock glyph after the secured networks in the list
shots[2][1][(11, 1)] = G[4]
shots[2][1][(19, 3)] = G[4]
shots[1][1][(19, 0)] = ' '
# the wrong-password row is drawn with the text centred like the firmware
LH = 40
rows_n = (len(shots) + 1) // 2
sheet = Image.new('RGB', (W * 2 + 30, (H + LH) * rows_n + 20), (245, 245, 245))
d = ImageDraw.Draw(sheet)
for i, (label, cells) in enumerate(shots):
    x = 10 + (i % 2) * (W + 10); y = 10 + (i // 2) * (H + LH)
    d.text((x + 4, y + 6), label, font=lfont, fill=(30, 30, 30))
    sheet.paste(screen(cells), (x, y + LH))
sheet.save(out)
print(out)
