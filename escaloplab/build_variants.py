"""Builds the v80 firmware for the standard printer, every variant and every printer file.

    python escaloplab/build_variants.py              # all of them
    python escaloplab/build_variants.py 300-direct   # only some ("standard" is the one without a variant)

Writes escaloplab/out/EscalopV80_Firmware_<version>[_<name>].hex. Uses `pio` from PATH, or the PIO environment variable.
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MARLIN = ROOT / 'Marlin'
OUT = ROOT / 'escaloplab' / 'out'
HOOK = MARLIN / 'escalop_variant.h'
NAME = re.compile(r'^[a-z0-9-]{1,16}$')


def targets():
    found = {'standard': None}
    for folder in ('variants', 'printers'):
        for f in sorted((ROOT / 'escaloplab' / folder).glob('*.h')):
            if not NAME.match(f.stem):
                sys.exit(f'bad name {f.name}: lowercase letters, digits and "-", at most 16 characters')
            if f.stem in found:
                sys.exit(f'{f.stem} exists twice')
            found[f.stem] = f
    return found


def main():
    version = re.search(r'#define ESCALOPLAB_FW_VERSION "([0-9.]+)"', (MARLIN / 'Configuration.h').read_text(encoding='utf-8')).group(1)
    pio = os.environ.get('PIO', 'pio')
    all_targets = targets()
    wanted = sys.argv[1:] or list(all_targets)
    for name in wanted:
        if name not in all_targets:
            sys.exit(f'unknown build {name}; known: {", ".join(all_targets)}')
    OUT.mkdir(exist_ok=True)
    try:
        for name in wanted:
            src = all_targets[name]
            if src is None:
                HOOK.write_text('// standard v80: no changes\n', encoding='utf-8')
            else:
                rel = os.path.relpath(src, MARLIN).replace('\\', '/')
                HOOK.write_text(f'// written by escaloplab/build_variants.py\n#include "{rel}"\n', encoding='utf-8')
            print(f'=== {name} ===', flush=True)
            subprocess.run([pio, 'run', '-e', 'mega2560'], cwd=ROOT, check=True)
            dest = OUT / (f'EscalopV80_Firmware_{version}.hex' if src is None else f'EscalopV80_Firmware_{version}_{name}.hex')
            shutil.copyfile(ROOT / '.pio' / 'build' / 'mega2560' / 'firmware.hex', dest)
            print(f'--> {dest.name}', flush=True)
    finally:
        HOOK.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
