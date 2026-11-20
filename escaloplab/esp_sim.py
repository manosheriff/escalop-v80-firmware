"""
Play the ESP-01 Wi-Fi module over USB, to test Marlin's side of PROTOCOL.md without the module.

The ESP-01 and USB share the Mega's Serial0, so a PC on the USB port at 1 000 000 baud sees and
sends exactly what the module would. Opening the port resets the Mega; the script waits for the
'//ESC:INFO ... boot=1' line first (use --no-wait to skip).

    pip install pyserial
    python escaloplab/esp_sim.py --port COM5 info
    python escaloplab/esp_sim.py --port COM5 ap                    # start-up setup screen (within 60 s of boot)
    python escaloplab/esp_sim.py --port COM5 wifi                  # scan list -> type a password on the printer
                                                                   #   -> BADPW once, then ON
    python escaloplab/esp_sim.py --port COM5 offer 1.7.1           # firmware offer, then Update/Later on the LCD
    python escaloplab/esp_sim.py --port COM5 notice UPTODATE
    python escaloplab/esp_sim.py --port COM5 upload [file.gco]     # M9110, 2 MB test file by default, with a
                                                                   #   corrupted packet and a 9 s pause
    python escaloplab/esp_sim.py --port COM5 upload --check E:/TEST.GCO   # compare with the card afterwards
    python escaloplab/esp_sim.py --port COM5 timeout               # M9110 then silence: expects '#X TIMEOUT'
    python escaloplab/esp_sim.py --port COM5 abort                 # expects '#X ABORT'
    python escaloplab/esp_sim.py --port COM5 sdfw firmware.hex     # answer 'Update from SD card' (start it on
                                                                   #   the LCD) and check every byte
    python escaloplab/esp_sim.py --port COM5 monitor               # just print what Marlin sends
"""
import argparse, binascii, os, random, struct, sys, time

try:
    import serial
except ImportError:
    sys.exit('pyserial is missing: pip install pyserial')

BAUD = 1_000_000


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)."""
    return binascii.crc_hqx(data, 0xFFFF)


assert crc16(b'123456789') == 0x29B1


def hexs(text: str) -> str:
    return text.encode('utf-8').hex().upper() if text else '-'


class Marlin:
    def __init__(self, port, wait_boot=True):
        self.s = serial.Serial(port, BAUD, timeout=0.05)
        self.buf = b''
        if wait_boot:
            print('waiting for Marlin to start (opening the port resets the board)...')
            if not self.wait(lambda l: l.startswith('//ESC:INFO') and 'boot=1' in l, 30):
                print('!! no "//ESC:INFO ... boot=1" within 30 s (is v1.7.0 flashed, and the port right?)')

    # --- lines ---
    def line(self, timeout):
        end = time.time() + timeout
        while time.time() < end:
            if b'\n' in self.buf:
                l, self.buf = self.buf.split(b'\n', 1)
                l = l.decode('utf-8', 'replace').rstrip('\r')
                print('  <', l)
                return l
            self.buf += self.s.read(256)
        return None

    def wait(self, pred, timeout):
        end = time.time() + timeout
        while time.time() < end:
            l = self.line(end - time.time())
            if l is not None and pred(l):
                return l
        return None

    def send(self, cmd):
        print('  >', cmd)
        self.s.write((cmd + '\n').encode())

    def gcode(self, cmd, timeout=5):
        """Send a G-code and wait for its 'ok'."""
        self.send(cmd)
        return self.wait(lambda l: l.startswith('ok'), timeout)

    def info(self):
        self.send('M9100')
        l = self.wait(lambda l: l.startswith('//ESC:INFO'), 5)
        return dict(kv.split('=', 1) for kv in l.split()[1:]) if l else None

    # --- raw ---
    def hash_line(self, timeout):
        """Next line starting with '#' (others are printed and ignored, as the ESP-01 does)."""
        return self.wait(lambda l: l.startswith('#'), timeout)

    def flush(self):
        time.sleep(0.05)
        self.buf += self.s.read(4096)


def cmd_info(m, a):
    print(m.info())


def cmd_ap(m, a):
    m.gcode('M9107 00123')
    m.gcode('M9101 AP %s -' % hexs('Escalop-v80-00123'))
    print('The printer should show "Wi-Fi setup / On your phone join: / Escalop-v80-00123" (only within 60 s of start-up).')


def cmd_wifi(m, a):
    nets = [('MyWiFi 5G', -50, 1), ('Guest', -70, 0), ('Neighbour with a long name', -82, 1)]
    m.gcode('M9107 00123')
    m.gcode('M9101 OFF - -')
    print('On the printer: Menu > Wi-Fi > Choose network ...')
    if not m.wait(lambda l: l.strip() == '//ESC:SCAN', 120):
        return print('!! no //ESC:SCAN')
    m.gcode('M9102 %d' % len(nets))
    for i, (name, rssi, lock) in enumerate(nets):
        m.gcode('M9103 %d %d %d %s' % (i, rssi, lock, hexs(name)))
    bad_once = True
    while True:
        l = m.wait(lambda l: l.startswith('//ESC:JOIN'), 300)
        if not l:
            return print('!! no //ESC:JOIN')
        _, idx, pw = l.split()
        idx = int(idx)
        password = '' if pw == '-' else bytes.fromhex(pw).decode('utf-8', 'replace')
        print('   join %r with password %r' % (nets[idx][0], password))
        m.gcode('M9101 JOIN %s -' % hexs(nets[idx][0]))
        time.sleep(2)
        if nets[idx][2] and bad_once:
            bad_once = False
            m.gcode('M9101 BADPW %s -' % hexs(nets[idx][0]))
            print('Sent BADPW: choose Try again on the printer and type it again.')
            continue
        m.gcode('M9101 ON %s 192.168.1.57' % hexs(nets[idx][0]))
        print('Sent ON: the printer should say "Wi-Fi connected. IP: 192.168.1.57".')
        return


def cmd_offer(m, a):
    m.gcode('M9104 %s' % a.version)
    print('Go to the status screen: the offer appears there. Waiting for Update / Later ...')
    l = m.wait(lambda l: l.strip() in ('//ESC:UPDATE', '//ESC:LATER'), 300)
    if l and l.strip() == '//ESC:UPDATE':
        for pct in (20, 60, 100):
            m.gcode('M9105 CHECK %d' % pct); time.sleep(1)
        m.gcode('M9105 INSTALL 0')
        print('The printer shows "Installing firmware". A real module would reset it now.')
        time.sleep(3)
        m.gcode('M9106 OK %s' % a.version)


def cmd_notice(m, a):
    m.gcode('M9108 %s' % a.code)


def packet(data: bytes) -> bytes:
    head = struct.pack('<H', len(data))
    return head + data + struct.pack('<H', crc16(head + data))


def test_file(size=2 * 1024 * 1024) -> bytes:
    rnd = random.Random(42)
    lines, n = [], 0
    while n < size:
        l = 'G1 X%.3f Y%.3f E%.5f F%d ; %08x\n' % (rnd.uniform(0, 210), rnd.uniform(0, 210), rnd.uniform(0, 3), rnd.choice((1200, 3000, 6000)), rnd.getrandbits(32))
        lines.append(l); n += len(l)
    return ''.join(lines).encode()[:size]


def start_upload(m, name, size):
    i = m.info()
    if not i or i.get('sd') != '1' or i.get('printing') != '0':
        print('!! printer not ready for an upload:', i)
        return False
    m.send('M9110 %d %s' % (size, name))
    l = m.hash_line(10)
    if l != '#A':
        print('!! expected #A, got', l)
        return False
    return True


def cmd_upload(m, a):
    data = open(a.file, 'rb').read() if a.file else test_file()
    name = a.name
    if not start_upload(m, name, len(data)):
        return
    chunks = [data[i:i + 512] for i in range(0, len(data), 512)]
    t0 = time.time()
    resends = 0
    for n, chunk in enumerate(chunks):
        if n == 10 and a.pause:
            print('   (pausing %.1f s between packets)' % a.pause); time.sleep(a.pause)
        pkt = packet(chunk)
        if n == 3:                                      # one deliberately corrupted packet
            bad = bytearray(pkt); bad[10] ^= 0xFF
            m.s.write(bytes(bad))
            l = m.hash_line(5)
            print('   corrupted packet 3 ->', l, '(expected #R)')
            if l != '#R':
                return print('!! corrupted packet not refused')
        for attempt in range(5):
            m.s.write(pkt)
            l = m.hash_line(5)
            if l == '#A':
                break
            if l == '#R':
                resends += 1; continue
            return print('!! packet %d: %s' % (n, l))
        else:
            return print('!! packet %d refused 5 times' % n)
        if n % 256 == 0:
            print('   %d / %d KB' % (n // 2, len(chunks) // 2))
    m.s.write(packet(b''))
    l = m.hash_line(10)
    dt = time.time() - t0 - (a.pause or 0)
    print('   end ->', l)
    if l != '#D %d' % len(data):
        print('!! expected "#D %d"' % len(data))
    else:
        print('OK: %d bytes in %.1f s = %.1f KB/s (pause excluded), %d resends' % (len(data), dt, len(data) / 1024 / dt, resends))
    m.wait(lambda l: l.startswith('echo:M9110'), 2)
    if a.check:
        print('Move the SD card to this PC, then press Enter.'); input()
        same = open(a.check, 'rb').read() == data
        print('card file is byte-identical' if same else '!! card file differs')
    if not a.file:
        open('esp_sim_upload.gco', 'wb').write(data)
        print('(the test file is saved as esp_sim_upload.gco for comparison)')


def cmd_timeout(m, a):
    if not start_upload(m, 'TIMEOUT.GCO', 0):
        return
    m.s.write(packet(b'G28\n'))
    print('   first packet ->', m.hash_line(5))
    print('   silent for 11 s ...')
    l = m.hash_line(15)
    print('OK' if l == '#X TIMEOUT' else '!! expected "#X TIMEOUT", got %s' % l)


def cmd_abort(m, a):
    if not start_upload(m, 'ABORT.GCO', 0):
        return
    m.s.write(packet(b'G28\n'))
    print('   first packet ->', m.hash_line(5))
    m.s.write(b'\xff\xff')
    l = m.hash_line(5)
    print('OK' if l == '#X ABORT' else '!! expected "#X ABORT", got %s' % l)


def cmd_sdfw(m, a):
    ref = open(a.hex, 'rb').read() if a.hex else None
    print('On the printer: Menu > Firmware > Update from SD card > pick the .hex > Install ...')
    l = m.wait(lambda l: l.startswith('//ESC:SDFW'), 300)
    if not l:
        return print('!! no //ESC:SDFW')
    size = int(l.split()[1])
    print('   Marlin will send %d bytes' % size)
    m.s.write(b'#READY\n')
    got = bytearray()
    t0 = time.time()
    m.s.timeout = 6
    while len(got) < size:
        want = min(1024, size - len(got))
        block = m.s.read(want)
        if len(block) != want:
            return print('!! block at %d: only %d of %d bytes arrived' % (len(got), len(block), want))
        got += block
        m.s.write(b'#OK\n')
    m.s.timeout = 0.05
    dt = time.time() - t0
    extra = m.s.read(64)
    if extra:
        print('!! Marlin printed something during the transfer:', extra)
    print('   received %d bytes in %.1f s = %.1f KB/s' % (len(got), dt, len(got) / 1024 / dt))
    if ref is not None:
        same = bytes(got) == ref
        print('   byte-identical to %s: %s' % (a.hex, same))
    m.s.write(('#VALID %d\n' % size).encode() if a.valid else b'#INVALID test\n')
    print('Sent %s.' % ('#VALID (the printer waits 60 s for a reset that will not come)' if a.valid else '#INVALID: the printer should say the file is not valid'))
    m.wait(lambda l: False, 3)


def cmd_monitor(m, a):
    print('(Ctrl+C to stop)')
    try:
        while True:
            m.line(1)
    except KeyboardInterrupt:
        pass


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--port', required=True)
    p.add_argument('--no-wait', action='store_true', help="don't wait for the start-up INFO line")
    sub = p.add_subparsers(dest='cmd', required=True)
    sub.add_parser('info'); sub.add_parser('ap'); sub.add_parser('wifi'); sub.add_parser('timeout')
    sub.add_parser('abort'); sub.add_parser('monitor')
    s = sub.add_parser('offer'); s.add_argument('version')
    s = sub.add_parser('notice'); s.add_argument('code', choices='NOSD PRINTING UPTODATE NOINTERNET DLFAIL SDFWBAD RECEIVED'.split())
    s = sub.add_parser('upload'); s.add_argument('file', nargs='?'); s.add_argument('--name', default='TEST.GCO')
    s.add_argument('--pause', type=float, default=9.0, help='pause between packets 10 and 11 (must stay < 10 s)')
    s.add_argument('--check', help='path of the uploaded file on the SD card, mounted on this PC')
    s = sub.add_parser('sdfw'); s.add_argument('hex', nargs='?'); s.add_argument('--valid', action='store_true', help='answer #VALID instead of #INVALID')
    a = p.parse_args()
    m = Marlin(a.port, not a.no_wait)
    m.flush()
    globals()['cmd_' + a.cmd](m, a)


if __name__ == '__main__':
    main()
