/**
 * Escaloplab v80 Wi-Fi (ESP-01 on Serial0): Marlin's side of PROTOCOL.md (version 1)
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(ESCALOPLAB_WIFI)

#if !HAS_MARLINUI_MENU || !HAS_MEDIA
  #error "ESCALOPLAB_WIFI needs the LCD menus and SD card support."
#endif

#include "escaloplab_wifi.h"
#include "../lcd/menu/menu_escaloplab_wifi.h"
#include "../MarlinCore.h"
#include "../module/temperature.h"
#include "../module/planner.h"
#include "../module/motion.h"
#include "../module/printcounter.h"
#include "../sd/cardreader.h"
#if ENABLED(EMERGENCY_PARSER)
  #include "e_parser.h"
#endif
#ifdef __AVR__
  #include <util/crc16.h>
#endif

EscWifi escWifi;

bool             EscWifi::heard;
EscWifi::State   EscWifi::state;
uint8_t          EscWifi::state_seq;
char             EscWifi::ssid[ESC_WIFI_NAME_LEN + 1],
                 EscWifi::ap_name[ESC_WIFI_NAME_LEN + 1],
                 EscWifi::ip[16],
                 EscWifi::serial[9];
EscWifi::Scan    EscWifi::scan;
uint8_t          EscWifi::scan_count;
uint16_t         EscWifi::scan_got;
EscWifi::Net     EscWifi::nets[ESC_WIFI_MAX_NETS];
char             EscWifi::offer[12];
bool             EscWifi::offer_pending;
EscWifi::Stage   EscWifi::stage;
uint8_t          EscWifi::stage_pct, EscWifi::stage_seq, EscWifi::notice_seq, EscWifi::notice;
bool             EscWifi::settings_were_reset;
millis_t         EscWifi::boot_ms;

// One buffer for both raw transfers (M9110 packets, SD firmware blocks)
static uint8_t xfer_buf[512];

//
// Text helpers
//

static int8_t hexval(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Decode protocol hex ('-' = empty) into a display-safe string: printable ASCII is kept,
// every other UTF-8 character becomes one '?'. Truncated to 'len' characters.
// Returns false if 'hex' isn't valid hex (the command is then ignored).
static bool hex_to_display(const char *hex, char * const out, const uint8_t len) {
  if (!hex) return false;
  uint8_t n = 0;
  if (hex[0] == '-' && !hex[1]) { out[0] = '\0'; return true; }
  for (const char *h = hex; *h; h += 2) {
    const int8_t hi = hexval(h[0]), lo = h[1] ? hexval(h[1]) : -1;
    if (hi < 0 || lo < 0) return false;
  }
  for (const char *h = hex; *h && n < len; h += 2) {
    const uint8_t b = (hexval(h[0]) << 4) | hexval(h[1]);
    if (b >= 0x20 && b < 0x7F) out[n++] = char(b);
    else if (b < 0x80 || b >= 0xC0) out[n++] = '?';     // control byte, or the first byte of a UTF-8 character
    // 0x80..0xBF: UTF-8 continuation byte, already counted
  }
  out[n] = '\0';
  return true;
}

// Next space-separated token of 'p' (modified in place)
static char* token(char *&p) {
  while (*p == ' ') ++p;
  if (!*p) return nullptr;
  char * const s = p;
  while (*p && *p != ' ') ++p;
  if (*p) *p++ = '\0';
  return s;
}

static bool parse_int(const char *s, int32_t &v) {
  if (!s || !*s) return false;
  bool neg = false;
  if (*s == '-') { neg = true; ++s; if (!*s) return false; }
  int32_t r = 0;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9' || r > 99999999L) return false;
    r = r * 10 + (*s - '0');
  }
  v = neg ? -r : r;
  return true;
}

// Copy 's' if it is only letters and digits and fits in 'size'
static bool copy_alnum(char * const dst, const char *s, const uint8_t size) {
  if (!s || !*s || strlen(s) >= size) return false;
  for (const char *c = s; *c; ++c)
    if (!((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z'))) return false;
  strcpy(dst, s);
  return true;
}

static bool is_version(const char *s) {   // digits and dots only
  if (!s || !*s || strlen(s) > 11) return false;
  for (; *s; ++s) if (!((*s >= '0' && *s <= '9') || *s == '.')) return false;
  return true;
}

static bool is_ip(const char *s) {
  if (!s || !*s || strlen(s) > 15) return false;
  for (; *s; ++s) if (!((*s >= '0' && *s <= '9') || *s == '.')) return false;
  return true;
}

//
// Printer status, as reported to the ESP-01
//

bool EscWifi::is_printing() {
  return IS_SD_PRINTING() || IS_SD_PAUSED() || print_job_timer.isRunning() || print_job_timer.isPaused();
}

bool EscWifi::is_busy() {
  return thermalManager.degTargetHotend(0) > 0
      || thermalManager.degHotend(0) > 50
      #if HAS_HEATED_BED
        || thermalManager.degTargetBed() > 0
        || thermalManager.degBed() > 45
      #endif
      || planner.has_blocks_queued() || planner.movesplanned();
}

//
// Marlin -> ESP-01
//

void EscWifi::send(FSTR_P const line) {
  SERIAL_ECHOPGM("//ESC:");
  SERIAL_ECHOLNF(line);
}

void EscWifi::send_info(const bool at_boot) {
  const bool sd = IS_SD_INSERTED() && card.isMounted();
  SERIAL_ECHOLNPGM("//ESC:INFO proto=1 model=v80 fw=" ESCALOPLAB_FW_VERSION
    " sd=", int(sd), " printing=", int(is_printing()), " busy=", int(is_busy()), " boot=", int(at_boot));
}

void EscWifi::send_join(const uint8_t index, const char * const pass) {
  static const char hexdig[] PROGMEM = "0123456789ABCDEF";
  SERIAL_ECHOPGM("//ESC:JOIN ", index, " ");
  if (!pass || !*pass) SERIAL_CHAR('-');
  else for (const char *c = pass; *c; ++c) {
    SERIAL_CHAR(char(pgm_read_byte(&hexdig[uint8_t(*c) >> 4])));
    SERIAL_CHAR(char(pgm_read_byte(&hexdig[uint8_t(*c) & 0xF])));
  }
  SERIAL_EOL();
}

void EscWifi::boot() {
  boot_ms = millis();
  send_info(true);
}

//
// Notices (M9108)
//
static const char nt_nosd[]       PROGMEM = "NOSD",       tx_nosd[]       PROGMEM = "Insert the SD card to print over Wi-Fi",
                  nt_printing[]   PROGMEM = "PRINTING",   tx_printing[]   PROGMEM = "Printer is busy - can't receive a file while printing",
                  nt_uptodate[]   PROGMEM = "UPTODATE",   tx_uptodate[]   PROGMEM = "Firmware is up to date",
                  nt_nointernet[] PROGMEM = "NOINTERNET", tx_nointernet[] PROGMEM = "No internet - can't check for updates",
                  nt_dlfail[]     PROGMEM = "DLFAIL",     tx_dlfail[]     PROGMEM = "Firmware download failed. Try again later",
                  nt_sdfwbad[]    PROGMEM = "SDFWBAD",    tx_sdfwbad[]    PROGMEM = "This .hex file is not valid firmware",
                  nt_received[]   PROGMEM = "RECEIVED",   tx_received[]   PROGMEM = "Print file received";
static PGM_P const notice_codes[] PROGMEM = { nt_nosd, nt_printing, nt_uptodate, nt_nointernet, nt_dlfail, nt_sdfwbad, nt_received };
static PGM_P const notice_texts[] PROGMEM = { tx_nosd, tx_printing, tx_uptodate, tx_nointernet, tx_dlfail, tx_sdfwbad, tx_received };

PGM_P EscWifi::notice_text(const uint8_t code) {
  return code && code <= COUNT(notice_texts) ? (PGM_P)pgm_read_ptr(&notice_texts[code - 1]) : nullptr;
}

//
// ESP-01 -> Marlin: M9100 .. M9110
//
void EscWifi::gcode(const uint16_t codenum, char *arg) {
  static char empty[1];
  char *p = arg ? arg : empty;
  heard = true;

  switch (codenum) {

    case 9100: send_info(false); break;

    case 9101: {                                        // M9101 <state> <a> <b>
      char * const st = token(p), * const a = token(p), * const b = token(p);
      if (!st) break;
      State s;
           if (!strcmp_P(st, PSTR("AP")))    s = W_AP;
      else if (!strcmp_P(st, PSTR("JOIN")))  s = W_JOIN;
      else if (!strcmp_P(st, PSTR("ON")))    s = W_ON;
      else if (!strcmp_P(st, PSTR("OFF")))   s = W_OFF;
      else if (!strcmp_P(st, PSTR("BADPW"))) s = W_BADPW;
      else break;
      char name[ESC_WIFI_NAME_LEN + 1];
      if (!hex_to_display(a, name, ESC_WIFI_NAME_LEN)) break;
      if (s == W_ON && !is_ip(b)) break;
      if (s == W_AP) strcpy(ap_name, name); else strcpy(ssid, name);
      if (s == W_ON) strcpy(ip, b); else if (s != W_JOIN) ip[0] = '\0';
      state = s;
      ++state_seq;
      EscWifiUI::on_state();
    } break;

    case 9102: {                                        // M9102 <count>
      int32_t n;
      if (!parse_int(token(p), n) || !WITHIN(n, 0, ESC_WIFI_MAX_NETS)) break;
      scan_count = n;
      scan_got = 0;
      scan = n ? SCAN_LIST : SCAN_DONE;
      EscWifiUI::on_scan();
    } break;

    case 9103: {                                        // M9103 <index> <rssi> <lock> <ssidhex>
      if (scan != SCAN_LIST) break;
      int32_t idx, rssi, lock;
      if (!parse_int(token(p), idx) || !WITHIN(idx, 0, scan_count - 1)) break;
      if (!parse_int(token(p), rssi) || !parse_int(token(p), lock) || !WITHIN(lock, 0, 1)) break;
      Net &net = nets[idx];
      if (!hex_to_display(token(p), net.name, ESC_WIFI_NAME_LEN)) break;
      net.rssi = constrain(rssi, -127, 0);
      net.lock = lock;
      scan_got |= _BV(idx);
      if (scan_got == uint16_t(_BV(scan_count) - 1)) scan = SCAN_DONE;
      EscWifiUI::on_scan();
    } break;

    case 9104: {                                        // M9104 <version>
      const char * const v = token(p);
      if (!is_version(v)) break;
      strcpy(offer, v);
      offer_pending = true;
    } break;

    case 9105: {                                        // M9105 <stage> <percent>
      const char * const st = token(p);
      int32_t pct;
      if (!st || !parse_int(token(p), pct)) break;
      Stage s;
           if (!strcmp_P(st, PSTR("DOWNLOAD"))) s = UP_DOWNLOAD;
      else if (!strcmp_P(st, PSTR("CHECK")))    s = UP_CHECK;
      else if (!strcmp_P(st, PSTR("INSTALL")))  s = UP_INSTALL;
      else break;
      stage = s;
      stage_pct = constrain(pct, 0, 100);
      ++stage_seq;
      if (s == UP_INSTALL) {                            // The ESP-01 resets the board within 2 s
        thermalManager.disable_all_heaters();
        quickstop_stepper();
        EscWifiUI::on_install();
      }
    } break;

    case 9106: {                                        // M9106 <OK|FAIL> <version>
      const char * const r = token(p), * const v = token(p);
      if (!r || !is_version(v)) break;
      const bool ok = !strcmp_P(r, PSTR("OK"));
      if (!ok && strcmp_P(r, PSTR("FAIL"))) break;
      EscWifiUI::on_result(ok, v);
    } break;

    case 9107: {                                        // M9107 <serial>
      const char * const s = token(p);
      if (s && s[0] == '-' && !s[1]) strcpy_P(serial, PSTR("-"));
      else copy_alnum(serial, s, sizeof(serial));
    } break;

    case 9108: {                                        // M9108 <code>
      const char * const c = token(p);
      if (!c) break;
      for (uint8_t i = 0; i < COUNT(notice_codes); ++i)
        if (!strcmp_P(c, (PGM_P)pgm_read_ptr(&notice_codes[i]))) {
          notice = i + 1;
          ++notice_seq;
          EscWifiUI::on_notice(notice);
          break;
        }
    } break;

    case 9110: M9110(p); break;

    default: break;                                     // M9109: not used, ignored
  }
}

//////////////////////////////////////////////////////////////////////////////////
// Raw transfers
//////////////////////////////////////////////////////////////////////////////////

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF): avr-libc's xmodem step is exactly this polynomial
static inline uint16_t crc_step(uint16_t crc, const uint8_t b) {
  #ifdef __AVR__
    return _crc_xmodem_update(crc, b);
  #else
    crc ^= uint16_t(b) << 8;
    for (uint8_t i = 0; i < 8; ++i) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    return crc;
  #endif
}

// One byte from Serial0, or -1 after 'timeout_ms'
static inline int16_t rx_byte(const uint16_t timeout_ms) {
  int c = MYSERIAL1.read();
  if (c >= 0) return c;
  const millis_t start = millis();
  for (;;) {
    if ((c = MYSERIAL1.read()) >= 0) return c;
    if (millis() - start >= timeout_ms) return -1;
  }
}

// Wait until the line has been quiet for 'quiet_ms' (gives up after 2 s of noise)
static void rx_wait_quiet(const uint16_t quiet_ms) {
  const millis_t start = millis();
  millis_t last = start;
  while (millis() - last < quiet_ms && millis() - start < 2000) {
    if (MYSERIAL1.read() >= 0) last = millis();
    hal.watchdog_refresh();
  }
}

static void rx_flush() { rx_wait_quiet(20); }

// A whole input line (without '\r' / '\n') or -1 after 'timeout_ms'. Keeps the watchdog fed.
static int16_t rx_line(char * const buf, const uint8_t size, const uint16_t timeout_ms) {
  uint8_t n = 0;
  const millis_t start = millis();
  for (;;) {
    const int c = MYSERIAL1.read();
    if (c < 0) {
      hal.watchdog_refresh();
      if (millis() - start >= timeout_ms) return -1;
      continue;
    }
    if (c == '\r') continue;
    if (c == '\n') { buf[n] = '\0'; return n; }
    if (n < size - 1) buf[n++] = char(c);
  }
}

static bool line_is(const char * const line, PGM_P word) {   // 'word' exactly, or 'word ...'
  const uint8_t n = strlen_P(word);
  return !strncmp_P(line, word, n) && (line[n] == '\0' || line[n] == ' ');
}

//
// M9110 <size> <name>: fast binary upload of a print file to the SD card (PROTOCOL.md section 9)
//
void EscWifi::M9110(char *p) {
  int32_t size;
  const bool size_ok = parse_int(token(p), size) && size >= 0;
  const char * const name = token(p);

  if (!IS_SD_INSERTED() || !card.isMounted()) { SERIAL_ECHOLNPGM("#X NOSD"); return; }
  if (is_printing() || card.isFileOpen())     { SERIAL_ECHOLNPGM("#X BUSY"); return; }

  // Name: [A-Z0-9_]{1,8}.GCO
  bool name_ok = size_ok && name;
  if (name_ok) {
    const char * const dot = strchr(name, '.');
    name_ok = dot && WITHIN(dot - name, 1, 8) && !strcmp_P(dot, PSTR(".GCO"));
    for (const char *c = name; name_ok && c < dot; ++c)
      name_ok = (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_';
  }
  if (!name_ok) { SERIAL_ECHOLNPGM("#X ERR"); return; }

  card.cdroot();                                        // the ESP-01 starts the print with 'M23 <name>'
  card.removeFile(name);
  card.openFileWrite(name);                             // also turns the emergency parser off
  if (!card.isFileOpen()) { SERIAL_ECHOLNPGM("#X SDERR"); return; }
  TERN_(EMERGENCY_PARSER, emergency_parser.disable());  // binary data may contain "M112"

  EscWifiUI::transfer_progress(PSTR("Receiving over Wi-Fi"), name, 0, size);

  uint32_t total = 0;
  const millis_t t_start = millis();
  millis_t next_lcd = t_start + 500, next_temp = t_start;
  PGM_P fail = nullptr;                                 // "#X <reason>" when set
  bool done = false;

  SERIAL_ECHOLNPGM("#A");                               // ready for the first packet

  while (!done && !fail) {

    // Wait up to 10 s for a packet to start, keeping the heaters managed meanwhile
    int16_t c;
    const millis_t wait_start = millis();
    for (;;) {
      if ((c = MYSERIAL1.read()) >= 0) break;
      hal.watchdog_refresh();
      const millis_t now = millis();
      if (now - wait_start >= 10000UL) break;
      if (ELAPSED(now, next_temp)) { thermalManager.task(); next_temp = millis() + 100; }
    }
    if (c < 0) { fail = PSTR("TIMEOUT"); break; }

    // Read the packet in a tight loop: nothing slow until it's all in
    bool ok = true;
    const int16_t c2 = rx_byte(200);
    uint16_t len = 0;
    if (c2 < 0) ok = false;
    else {
      len = uint16_t(c) | (uint16_t(c2) << 8);
      if (len == 0xFFFF) { fail = PSTR("ABORT"); break; }
      if (len > sizeof(xfer_buf)) ok = false;
    }
    for (uint16_t i = 0; ok && i < len; ++i) {
      const int16_t d = rx_byte(200);
      if (d < 0) ok = false; else xfer_buf[i] = d;
    }
    int16_t clo = -1, chi = -1;
    if (ok) { clo = rx_byte(200); chi = clo < 0 ? -1 : rx_byte(200); ok = chi >= 0; }

    if (ok) {                                           // CRC over the two length bytes and the data
      uint16_t crc = crc_step(crc_step(0xFFFF, len & 0xFF), len >> 8);
      for (uint16_t i = 0; i < len; ++i) crc = crc_step(crc, xfer_buf[i]);
      ok = crc == (uint16_t(clo) | (uint16_t(chi) << 8));
    }

    if (!ok) {                                          // let the line go quiet, then ask for it again
      rx_wait_quiet(20);
      SERIAL_ECHOLNPGM("#R");
      continue;
    }

    if (len == 0) { done = true; break; }               // end of file

    if (card.write(xfer_buf, len) != int16_t(len)) { fail = PSTR("SDERR"); break; }
    total += len;

    // Slow work only here, between packets
    hal.watchdog_refresh();
    thermalManager.task();
    if (ELAPSED(millis(), next_lcd)) {
      EscWifiUI::transfer_progress(PSTR("Receiving over Wi-Fi"), name, total, size);
      next_lcd = millis() + 500;
    }
    SERIAL_ECHOLNPGM("#A");
  }

  card.closefile();                                     // re-enables the emergency parser
  if (fail) {
    card.removeFile(name);
    SERIAL_ECHOPGM("#X "); SERIAL_ECHOLNPGM_P(fail);
  }
  else
    SERIAL_ECHOLNPGM("#D ", total);

  rx_flush();
  TERN_(EMERGENCY_PARSER, emergency_parser.enable());

  const millis_t ms = _MAX(millis() - t_start, 1UL);
  SERIAL_ECHOLNPGM("echo:M9110 ", total, " bytes in ", ms, " ms = ", total / ms, " KB/s");

  EscWifiUI::transfer_done();
}

//
// Firmware update from the SD card: stream a .hex to the ESP-01 (PROTOCOL.md section 10)
//
// From '//ESC:SDFW' until the final answer nothing else may be printed on Serial0:
// no idle(), no thermalManager.task() (heaters are off; the temperature ISR still guards them).
//
PGM_P EscWifi::send_sd_firmware(const char * const dosname, const char * const longname) {
  if (is_printing())                    return PSTR("Can't update while printing");
  if (is_busy())                        return PSTR("Turn heaters off first");
  if (!card.isMounted())                return PSTR("Insert the SD card");

  thermalManager.disable_all_heaters();

  card.cdroot();
  card.openFileRead(dosname);
  if (!card.isFileOpen())               return PSTR("Can't read the file");
  const uint32_t size = card.getFileSize();

  rx_flush();
  SERIAL_ECHOLNPGM("//ESC:SDFW ", size);

  PGM_P result = nullptr;
  char line[32];

  // 1. Wait for the ESP-01 to be ready
  const millis_t t_ready = millis();
  for (;;) {
    const uint16_t left = 10000UL - _MIN(millis() - t_ready, 10000UL);
    if (!left || rx_line(line, sizeof(line), left) < 0) { result = PSTR("No Wi-Fi module found"); break; }
    if (line_is(line, PSTR("#READY"))) break;
    if (line_is(line, PSTR("#NO")))    { result = PSTR("Wi-Fi module not ready"); break; }
  }

  // 2. Send the file in 1024-byte blocks, each answered by #OK or #BAD
  uint32_t sent = 0;
  millis_t next_lcd = 0;
  while (!result && sent < size) {
    if (ELAPSED(millis(), next_lcd)) {
      EscWifiUI::transfer_progress(PSTR("Sending firmware"), longname, sent, size);
      next_lcd = millis() + 500;
    }
    uint16_t block = _MIN(1024UL, size - sent);
    while (block) {
      const uint16_t n = _MIN(block, uint16_t(sizeof(xfer_buf)));
      if (card.read(xfer_buf, n) != int16_t(n)) { result = PSTR("Can't read the file"); break; }
      for (uint16_t i = 0; i < n; ++i) MYSERIAL1.write(xfer_buf[i]);
      sent += n; block -= n;
      hal.watchdog_refresh();
    }
    if (result) break;                                  // a short block: the ESP-01 times out on its side

    const millis_t t_ack = millis();
    for (;;) {
      const uint16_t left = 5000UL - _MIN(millis() - t_ack, 5000UL);
      if (!left || rx_line(line, sizeof(line), left) < 0) { result = PSTR("Transfer error, try again"); break; }
      if (line_is(line, PSTR("#OK")))  break;
      if (line_is(line, PSTR("#BAD"))) { result = PSTR("Transfer error, try again"); break; }
    }
  }
  card.closefile();

  // 3. The ESP-01's verdict
  if (!result) {
    EscWifiUI::transfer_progress(PSTR("Sending firmware"), longname, size, size);
    const millis_t t_valid = millis();
    for (;;) {
      const uint16_t left = 15000UL - _MIN(millis() - t_valid, 15000UL);
      if (!left || rx_line(line, sizeof(line), left) < 0) { result = PSTR("No answer from the Wi-Fi module"); break; }
      if (line_is(line, PSTR("#VALID")))   break;
      if (line_is(line, PSTR("#INVALID"))) { result = PSTR("This .hex file is not valid firmware"); break; }
    }
  }

  if (!result) {
    // The ESP-01 resets the board within 2 s and installs. Wait quietly; give up after 60 s.
    EscWifiUI::on_install();
    EscWifiUI::draw_installing();
    const millis_t t_install = millis();
    while (millis() - t_install < 60000UL) hal.watchdog_refresh();
    result = PSTR("The update didn't start. The old firmware is still installed");
  }

  rx_flush();
  return result;
}

#endif // ESCALOPLAB_WIFI
