/**
 * Escaloplab v80 Wi-Fi: LCD screens (20x4)
 *
 *   Main menu > Wi-Fi       status, Choose network (scan > list > password > connect),
 *                           Set up with phone, Forget Wi-Fi, serial number
 *   Main menu > Firmware    version, Check for updates, Update from SD card
 *   Pop-ups                 setup hotspot at start-up, update offer, update progress,
 *                           update result, notices
 *
 * Menus are Marlin menus like the rest; the full-screen pages are drawn directly
 * (every row padded to 20 columns, so they never need a clear).
 */
#include "../../inc/MarlinConfigPre.h"

#if ALL(ESCALOPLAB_WIFI, HAS_MARLINUI_MENU)

#include "menu_item.h"
#include "menu_escaloplab_wifi.h"
#include "../../feature/escaloplab_wifi.h"
#include "../../sd/cardreader.h"
#include "../../MarlinCore.h"

void esc_lcd_glyph(const uint8_t slot, const uint8_t * const pgm);   // marlinui_HD44780.cpp

#define W LCD_WIDTH

namespace EscWifiUI {

  typedef void (*fn_t)();

  static void screen_message();
  static void screen_choice();
  static void screen_hotspot();
  static void screen_searching();
  static void screen_password();
  static void screen_connecting();
  static void screen_checking();
  static void screen_updating();
  static void screen_installing();
  static void menu_networks();
  static void menu_sd_hex();

  //////////////////////////////////////////////////////////////////////////////
  // Drawing helpers
  //////////////////////////////////////////////////////////////////////////////

  static bool dirty;          // the current page needs drawing
  static millis_t t0;         // when the current page started waiting

  // True once, on the first call of a page
  static bool entered() {
    if (!ui.screen_changed) return false;
    ui.screen_changed = false;
    dirty = true;
    return true;
  }

  static void keep_calling() { ui.refresh(LCDVIEW_CALL_NO_REDRAW); }    // for timeouts and animation

  // A whole row from RAM or PROGMEM text, padded to the full width
  static void put_row(const uint8_t r, const char * const s, const bool pgm, const bool center) {
    uint8_t n = s ? (pgm ? strlen_P(s) : strlen(s)) : 0;
    NOMORE(n, W);
    const uint8_t c0 = center ? (W - n) / 2 : 0;
    lcd_moveto(0, r);
    for (uint8_t c = 0; c < W; ++c) {
      char ch = ' ';
      if (c >= c0 && c < c0 + n) ch = pgm ? char(pgm_read_byte(&s[c - c0])) : s[c - c0];
      lcd_put_lchar(lchar_t(uint8_t(ch)));
    }
  }
  static void row_P(const uint8_t r, PGM_P s, const bool center=true)        { put_row(r, s, true, center); }
  static void row(const uint8_t r, const char * const s, const bool center=true) { put_row(r, s, false, center); }

  // "Searching" dots: 0..3, and whether they changed since the last call
  static bool dots_changed(uint8_t &d) {
    static uint8_t last = 0xFF;
    d = (millis() / 400) % 4;
    if (d == last) return false;
    last = d;
    return true;
  }
  static void row_dots(const uint8_t r, PGM_P s, const uint8_t d) {   // "text..." with the dots growing, text kept still
    char line[W + 1];
    const uint8_t n = _MIN(strlen_P(s), uint8_t(W - 3));
    memcpy_P(line, s, n);
    for (uint8_t i = 0; i < 3; ++i) line[n + i] = i < d ? '.' : ' ';
    line[n + 3] = '\0';
    row(r, line);
  }

  // Message text: a PROGMEM template with '$' replaced by 'arg'
  static char txt[64];
  static void expand(PGM_P tpl, const char * const arg) {
    uint8_t n = 0;
    for (char ch; tpl && (ch = pgm_read_byte(tpl)) && n < sizeof(txt) - 1; ++tpl) {
      if (ch == '$' && arg) for (const char *a = arg; *a && n < sizeof(txt) - 1; ++a) txt[n++] = *a;
      else txt[n++] = ch;
    }
    txt[n] = '\0';
  }

  // Next word-wrapped line of 'p' (advanced past it), as long as the screen is wide
  static uint8_t wrap_line(const char *&p, char * const line) {
    while (*p == ' ') ++p;
    uint8_t take = strlen(p);
    if (take > W) {
      uint8_t brk = 0;
      for (uint8_t i = 1; i <= W; ++i) if (p[i] == ' ') brk = i;
      take = brk ? brk : W;                     // a word longer than the screen is cut
    }
    uint8_t len = take;
    while (len && p[len - 1] == ' ') --len;
    memcpy(line, p, len);
    line[len] = '\0';
    p += take;
    return len;
  }

  // 'txt' wrapped and centered (both ways) in rows [0, rows)
  static void draw_wrapped(const uint8_t rows) {
    char line[W + 1];
    uint8_t lines = 0;
    for (const char *p = txt; *p && lines < rows; ++lines) { wrap_line(p, line); while (*p == ' ') ++p; }
    const uint8_t top = (rows - lines) / 2;
    const char *p = txt;
    for (uint8_t r = 0; r < rows; ++r) {
      if (r >= top && r < top + lines) { wrap_line(p, line); row(r, line); }
      else row(r, nullptr);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Message page: text + [ OK ]
  //////////////////////////////////////////////////////////////////////////////

  enum After : uint8_t { A_PREV, A_STATUS, A_WIFI, A_FIRMWARE };
  static After msg_after;
  static PGM_P msg_then;          // a second message to show after this one
  static millis_t msg_until;      // closes by itself at this time (0 = never)

  static void go(const After a) {
    switch (a) {
      case A_PREV:     ui.goto_previous_screen(); break;
      case A_STATUS:   ui.return_to_status(); break;
      case A_WIFI:     ui.goto_screen(menu_escalop_wifi); break;
      case A_FIRMWARE: ui.goto_screen(menu_escalop_firmware); break;
    }
  }

  static void show_message(PGM_P text, const char * const arg, const After after, const uint8_t timeout_s=0, PGM_P then=nullptr) {
    expand(text, arg);
    msg_then = then;
    msg_until = timeout_s ? millis() + timeout_s * 1000UL : 0;
    if (ui.currentScreen == screen_message) {   // replace the message on screen; keep where it returns to
      if (after != A_PREV) msg_after = after;
      dirty = true;
      return;
    }
    msg_after = after;
    if (after == A_PREV) ui.push_current_screen();
    ui.goto_screen(screen_message);
  }

  static void screen_message() {
    entered();
    if (ui.use_click() || (msg_until && ELAPSED(millis(), msg_until))) {
      if (!msg_then) return go(msg_after);
      expand(msg_then, nullptr);
      msg_then = nullptr;
      msg_until = 0;
      dirty = true;
    }
    if (dirty) {
      dirty = false;
      draw_wrapped(3);
      row_P(3, PSTR("[ OK ]"));
    }
    keep_calling();
  }

  //////////////////////////////////////////////////////////////////////////////
  // Choice page: text + two buttons
  //////////////////////////////////////////////////////////////////////////////

  static PGM_P ch_yes;            // PGM_P is a macro: one name per declaration
  static PGM_P ch_no;
  static fn_t ch_yes_fn, ch_no_fn;
  static bool ch_sel;             // true = left button ("yes")

  static void show_choice(PGM_P text, const char * const arg, PGM_P yes, PGM_P no, fn_t yes_fn, fn_t no_fn, const bool default_yes) {
    expand(text, arg);
    ch_yes = yes; ch_no = no; ch_yes_fn = yes_fn; ch_no_fn = no_fn; ch_sel = default_yes;
    if (ui.currentScreen == screen_choice) dirty = true; else ui.goto_screen(screen_choice);
  }

  static void screen_choice() {
    entered();
    ui.defer_status_screen();                     // wait for an answer
    ui.encoder_direction_select();
    const int32_t d = int32_t(ui.encoderPosition);
    if (d) { ch_sel = d < 0; ui.encoderPosition = 0; dirty = true; }
    if (ui.use_click()) {
      ui.defer_status_screen(false);
      const fn_t f = ch_sel ? ch_yes_fn : ch_no_fn;
      if (f) f(); else ui.return_to_status();
      return;
    }
    if (dirty) {
      dirty = false;
      draw_wrapped(3);
      char line[W + 1];
      memset(line, ' ', W); line[W] = '\0';
      const uint8_t ly = strlen_P(ch_yes), ln = strlen_P(ch_no), rn = W - 2 - ln;
      line[0] = ch_sel ? '[' : ' ';  memcpy_P(&line[1], ch_yes, ly);       line[1 + ly] = ch_sel ? ']' : ' ';
      line[rn] = ch_sel ? ' ' : '['; memcpy_P(&line[rn + 1], ch_no, ln);   line[W - 1]  = ch_sel ? ' ' : ']';
      row(3, line, false);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  // Setup hotspot page (at start-up, or from Set up with phone)
  //////////////////////////////////////////////////////////////////////////////

  static bool ap_from_boot, ap_shown;
  static uint8_t ap_seq;

  static void screen_hotspot() {
    if (entered()) ap_seq = escWifi.state_seq;
    if (escWifi.state_seq != ap_seq) {
      ap_seq = escWifi.state_seq;
      if (escWifi.state == EscWifi::W_ON)       // set up from the phone meanwhile
        return show_message(PSTR("Wi-Fi connected. IP: $"), escWifi.ip, ap_from_boot ? A_STATUS : A_WIFI, 10);
      dirty = true;
    }
    if (ui.use_click()) {
      if (ap_from_boot) ui.goto_previous_screen(); else ui.goto_screen(menu_escalop_wifi);
      return;
    }
    if (dirty) {
      dirty = false;
      row_P(0, PSTR("Wi-Fi setup"));
      row_P(1, PSTR("On your phone join:"));
      if (escWifi.state == EscWifi::W_AP && escWifi.ap_name[0]) row(2, escWifi.ap_name);
      else row_P(2, PSTR("Starting hotspot..."));
      row_P(3, ap_from_boot ? PSTR("Or: Menu > Wi-Fi") : PSTR("Click when done"));
    }
    keep_calling();
  }

  //////////////////////////////////////////////////////////////////////////////
  // Choose network: search, list, password, connect
  //////////////////////////////////////////////////////////////////////////////

  static void start_scan() {
    escWifi.scan = EscWifi::SCAN_WAIT;
    escWifi.send(F("SCAN"));
    ui.goto_screen(screen_searching);
  }

  static void screen_searching() {
    if (entered()) t0 = millis();
    if (escWifi.scan == EscWifi::SCAN_DONE) {
      if (escWifi.scan_count) return ui.goto_screen(menu_networks);
      return show_message(PSTR("No Wi-Fi networks found"), nullptr, A_WIFI);
    }
    if (ELAPSED(millis(), t0 + 15000UL))
      return show_message(escWifi.heard ? PSTR("No answer from the Wi-Fi module") : PSTR("No Wi-Fi module found"), nullptr, A_WIFI);
    if (ui.use_click()) return ui.goto_screen(menu_escalop_wifi);
    uint8_t d;
    if (dots_changed(d) || dirty) {
      dirty = false;
      row(0, nullptr);
      row_P(1, PSTR("Searching for"));
      row_dots(2, PSTR("Wi-Fi networks"), d);
      row(3, nullptr);
    }
    keep_calling();
  }

  static const PROGMEM uint8_t g_lock[8] = { B01110, B10001, B10001, B11111, B11011, B11011, B11111, B00000 };

  static uint8_t join_index, join_seq;
  static char pass[ESC_WIFI_PASS_LEN + 1];

  static void start_join() {
    escWifi.send_join(join_index, pass);
    join_seq = escWifi.state_seq;
    ui.goto_screen(screen_connecting);
  }

  static void pick_network() {
    join_index = MenuItemBase::itemIndex;
    pass[0] = '\0';
    if (escWifi.nets[join_index].lock) ui.goto_screen(screen_password); else start_join();
  }

  // Networks as received (strongest first), with a lock on secured ones
  static void menu_networks() {
    if (ui.screen_changed) esc_lcd_glyph(LCD_STR_REFRESH[0], g_lock);   // that slot is only used by the SD menu
    START_MENU();
    ACTION_ITEM_F(GET_TEXT_F(MSG_BACK), []{ ui.goto_screen(menu_escalop_wifi); });
    for (uint8_t i = 0; i < escWifi.scan_count; ++i) {
      if (!MY_LINE()) { SKIP_ITEM(); continue; }
      char lbl[W + 1];
      const EscWifi::Net &net = escWifi.nets[i];
      const uint8_t n = _MIN(strlen(net.name), uint8_t(W - 4));
      memcpy(lbl, net.name, n);
      lbl[n] = '\0';
      if (net.lock) { lbl[n] = ' '; lbl[n + 1] = LCD_STR_REFRESH[0]; lbl[n + 2] = '\0'; }
      ACTION_ITEM_N_S_F(i, lbl, F("$"), pick_network);
    }
    END_MENU();
  }

  //
  // Password editor
  //
  //   |L MyNetwork         |   L = lock
  //   |hunter2_            |   typed so far (scrolls), blinking cursor
  //   |stuvwxyz[A]BCDEFGHI |   the knob turns this strip; click adds the one in brackets
  //   |Add: A          7/63|
  //
  // Strip order: a-z A-Z 0-9 space symbols, then Delete, Done, Cancel. It wraps around and
  // keeps its position between visits.
  //
  static const char pw_symbols[] PROGMEM = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
  enum : uint8_t { PW_SPACE = 62, PW_SYM = 63, PW_DEL = PW_SYM + 32, PW_DONE, PW_CANCEL, PW_ITEMS };
  enum : uint8_t { PG_SPACE, PG_DEL, PG_DONE, PG_CANCEL, PG_LOCK, PG_BSLASH, PG_TILDE, PG_COUNT };
  static const PROGMEM uint8_t pw_glyphs[PG_COUNT][8] = {
    { B00000, B00000, B00000, B00000, B00000, B10001, B11111, B00000 },   // space
    { B00000, B00100, B01000, B11111, B01000, B00100, B00000, B00000 },   // delete: arrow left
    { B00000, B00001, B00011, B10110, B11100, B01000, B00000, B00000 },   // done: tick
    { B00000, B10001, B01010, B00100, B01010, B10001, B00000, B00000 },   // cancel: cross
    { B01110, B10001, B10001, B11111, B11011, B11011, B11111, B00000 },   // lock
    { B00000, B10000, B01000, B00100, B00010, B00001, B00000, B00000 },   // '\' (the LCD's ROM may show a yen sign)
    { B00000, B00000, B01000, B10101, B00010, B00000, B00000, B00000 }    // '~' (the LCD's ROM may show an arrow)
  };
  static uint8_t pick;

  static char item_char(const uint8_t i) {
    if (i < 26) return 'a' + i;
    if (i < 52) return 'A' + i - 26;
    if (i < 62) return '0' + i - 52;
    if (i == PW_SPACE) return ' ';
    if (i < PW_DEL) return char(pgm_read_byte(&pw_symbols[i - PW_SYM]));
    return '\0';
  }

  static uint8_t show_char(const char c) {           // what to put on the LCD for a password character
    return c == ' ' ? PG_SPACE : c == '\\' ? PG_BSLASH : c == '~' ? PG_TILDE : uint8_t(c);
  }

  static uint8_t item_cell(const uint8_t i) {
    return i == PW_DEL ? PG_DEL : i == PW_DONE ? PG_DONE : i == PW_CANCEL ? PG_CANCEL : show_char(item_char(i));
  }

  static void draw_cells(const uint8_t r, const uint8_t * const cells) {
    lcd_moveto(0, r);
    for (uint8_t c = 0; c < W; ++c) lcd_put_lchar(lchar_t(cells[c]));
  }

  static void screen_password() {
    if (entered()) {
      for (uint8_t i = 0; i < PG_COUNT; ++i) esc_lcd_glyph(i, pw_glyphs[i]);
      ui.encoderPosition = 0;
    }
    ui.encoder_direction_menus();
    ENCODER_RATE_MULTIPLY(false);
    ui.defer_status_screen();                     // typing a long password takes a while; don't lose it

    const int32_t d = int32_t(ui.encoderPosition), steps = d / int32_t(ENCODER_STEPS_PER_MENU_ITEM);
    if (steps) {
      pick = (int16_t(pick) + steps % PW_ITEMS + PW_ITEMS) % PW_ITEMS;
      ui.encoderPosition = d - steps * int32_t(ENCODER_STEPS_PER_MENU_ITEM);
      dirty = true;
    }

    if (ui.use_click()) {
      const uint8_t len = strlen(pass);
      if (pick == PW_CANCEL || pick == PW_DONE) ui.defer_status_screen(false);
      if (pick == PW_CANCEL) return ui.goto_screen(menu_networks);
      if (pick == PW_DONE) return start_join();
      if (pick == PW_DEL) { if (len) pass[len - 1] = '\0'; }
      else if (len < ESC_WIFI_PASS_LEN) { pass[len] = item_char(pick); pass[len + 1] = '\0'; }
      dirty = true;
    }

    static bool last_blink;
    const bool blink = (millis() / 500) & 1;
    if (blink != last_blink) { last_blink = blink; dirty = true; }

    if (dirty) {
      dirty = false;
      uint8_t cells[W];
      const uint8_t len = strlen(pass);

      // Row 0: the network
      memset(cells, ' ', W);
      cells[0] = PG_LOCK;
      const char * const name = escWifi.nets[join_index].name;
      for (uint8_t c = 2, i = 0; c < W && name[i]; ++c, ++i) cells[c] = uint8_t(name[i]);
      draw_cells(0, cells);

      // Row 1: the password so far, scrolled to keep the cursor in view
      memset(cells, ' ', W);
      const uint8_t first = len > W - 1 ? len - (W - 1) : 0;
      for (uint8_t c = 0; c < W; ++c) {
        const uint8_t i = first + c;
        if (i < len) cells[c] = show_char(pass[i]);
        else if (i == len) cells[c] = blink ? '_' : ' ';
      }
      draw_cells(1, cells);

      // Row 2: the strip, current item in brackets at column 10
      //   columns 0-8: items -9..-1, 9: '[', 10: current, 11: ']', 12-19: items +1..+8
      for (uint8_t c = 0; c < W; ++c) {
        const int8_t off = c < 9 ? int8_t(c) - 9 : c == 10 ? 0 : int8_t(c) - 11;
        cells[c] = c == 9 ? '[' : c == 11 ? ']' : item_cell((int16_t(pick) + off + PW_ITEMS) % PW_ITEMS);
      }
      draw_cells(2, cells);

      // Row 3: what a click does, and the length
      char line[W + 1];
      memset(line, ' ', W); line[W] = '\0';
      PGM_P what = nullptr;
      switch (pick) {
        case PW_DEL:    what = PSTR("Delete last");  break;
        case PW_DONE:   what = PSTR("Done: connect"); break;
        case PW_CANCEL: what = PSTR("Cancel");       break;
        case PW_SPACE:  what = PSTR("Add a space");  break;
        default:        what = PSTR("Add:");         break;
      }
      memcpy_P(line, what, strlen_P(what));
      char count[7];
      sprintf_P(count, PSTR("%u/%u"), len, ESC_WIFI_PASS_LEN);
      memcpy(&line[W - strlen(count)], count, strlen(count));
      for (uint8_t c = 0; c < W; ++c) cells[c] = uint8_t(line[c]);
      if (pick < PW_DEL && pick != PW_SPACE) cells[5] = item_cell(pick);
      draw_cells(3, cells);
    }
    keep_calling();
  }

  static void screen_connecting() {
    if (entered()) t0 = millis();
    if (escWifi.state_seq != join_seq) {
      join_seq = escWifi.state_seq;
      switch (escWifi.state) {
        case EscWifi::W_ON:
          return show_message(PSTR("Wi-Fi connected. IP: $"), escWifi.ip, A_WIFI, 10);
        case EscWifi::W_BADPW:
          return show_choice(PSTR("Wrong Wi-Fi password"), nullptr, PSTR("Try again"), PSTR("Cancel"),
                             []{ ui.goto_screen(screen_password); }, []{ ui.goto_screen(menu_escalop_wifi); }, true);
        case EscWifi::W_OFF: case EscWifi::W_AP:
          return show_message(PSTR("Can't connect to $"), escWifi.nets[join_index].name, A_WIFI);
        default: break;                           // JOIN: still connecting
      }
    }
    if (ELAPSED(millis(), t0 + 45000UL)) return show_message(PSTR("No answer from the Wi-Fi module"), nullptr, A_WIFI);
    uint8_t d;
    if (dots_changed(d) || dirty) {
      dirty = false;
      row(0, nullptr);
      row_P(1, PSTR("Connecting to"));
      row(2, escWifi.nets[join_index].name);
      row_dots(3, PSTR(""), d);
    }
    keep_calling();
  }

  //////////////////////////////////////////////////////////////////////////////
  // Firmware: check, offer, progress, install
  //////////////////////////////////////////////////////////////////////////////

  static uint8_t seen_notice, seen_stage;

  static void start_check() {
    escWifi.offer_pending = false;
    seen_notice = escWifi.notice_seq;
    seen_stage = escWifi.stage_seq;
    escWifi.send(F("CHECK"));
    ui.goto_screen(screen_checking);
  }

  static void accept_offer() {
    if (escWifi.is_printing()) return show_message(PSTR("Finish the print first, then update from Menu > Firmware"), nullptr, A_STATUS);
    escWifi.send(F("UPDATE"));
    seen_stage = escWifi.stage_seq;
    ui.goto_screen(screen_updating);
  }

  static void show_offer() {
    show_choice(PSTR("New firmware $ is available. Installs over Wi-Fi."), escWifi.offer,
                PSTR("Update"), PSTR("Later"), accept_offer, []{ escWifi.send(F("LATER")); ui.return_to_status(); }, true);
  }

  static PGM_P stage_name() {
    switch (escWifi.stage) {
      case EscWifi::UP_DOWNLOAD: return PSTR("Downloading");
      case EscWifi::UP_CHECK:    return PSTR("Checking");
      case EscWifi::UP_INSTALL:  return PSTR("Installing");
      default:                   return PSTR("");
    }
  }

  static void row_stage(const uint8_t r) {
    char line[W + 1];
    sprintf_P(line, PSTR("%S %u%%"), stage_name(), escWifi.stage_pct);
    row(r, line);
  }

  static void screen_checking() {
    if (entered()) t0 = millis();
    if (escWifi.offer_pending) { escWifi.offer_pending = false; return show_offer(); }
    if (escWifi.notice_seq != seen_notice) {
      seen_notice = escWifi.notice_seq;
      PGM_P const t = escWifi.notice_text(escWifi.notice);
      if (t) return show_message(t, nullptr, A_FIRMWARE);
    }
    if (escWifi.stage_seq != seen_stage) { seen_stage = escWifi.stage_seq; dirty = true; }
    const millis_t waited = millis() - t0;
    if (!escWifi.heard && waited > 5000UL) return show_message(PSTR("No Wi-Fi module found"), nullptr, A_FIRMWARE);
    if (waited > 120000UL) return show_message(PSTR("No answer from the Wi-Fi module"), nullptr, A_FIRMWARE);
    if (ui.use_click()) return ui.goto_screen(menu_escalop_firmware);
    uint8_t d;
    if (dots_changed(d) || dirty) {
      dirty = false;
      row(0, nullptr);
      row_P(1, PSTR("Checking for"));
      row_dots(2, PSTR("updates"), d);
      if (escWifi.stage == EscWifi::UP_DOWNLOAD) row_stage(3); else row(3, nullptr);
    }
    keep_calling();
  }

  static void screen_updating() {
    if (entered()) t0 = millis();
    ui.defer_status_screen();
    if (escWifi.stage_seq != seen_stage) { seen_stage = escWifi.stage_seq; t0 = millis(); dirty = true; }
    if (ELAPSED(millis(), t0 + 120000UL)) {
      ui.defer_status_screen(false);
      return show_message(PSTR("No answer from the Wi-Fi module"), nullptr, A_STATUS);
    }
    if (dirty) {
      dirty = false;
      row_P(0, PSTR("Updating over Wi-Fi"));
      row_P(1, PSTR("Don't turn off"));
      row_P(2, PSTR("the printer"));
      if (escWifi.stage) row_stage(3); else row(3, nullptr);
    }
    keep_calling();
  }

  void draw_installing() {
    // The board restarts into its bootloader to install: nothing drives the screen until the new firmware starts,
    // so it keeps this text or shows blocks. Say so beforehand.
    row_P(0, PSTR("Installing firmware"));
    row_P(1, PSTR("Screen may go blank"));
    row_P(2, PSTR("for about 1 minute"));
    row_P(3, PSTR("Don't turn off"));
  }

  static void screen_installing() {
    if (entered()) t0 = millis();
    ui.defer_status_screen();
    if (ELAPSED(millis(), t0 + 90000UL)) {        // the ESP-01 should have reset the board long ago
      ui.defer_status_screen(false);
      return show_message(PSTR("The update didn't start. The old firmware is still installed"), nullptr, A_STATUS);
    }
    if (dirty) { dirty = false; draw_installing(); }
    keep_calling();
  }

  //
  // Update from SD card: the .hex files in the card's root
  //
  #define ESC_MAX_HEX 12
  static int16_t hex_idx[ESC_MAX_HEX];
  static uint8_t hex_count;
  static char hex_dos[13], hex_long[W + 1];

  static void run_sd_update() {
    PGM_P const r = escWifi.send_sd_firmware(hex_dos, hex_long);
    show_message(r, nullptr, A_FIRMWARE);
  }

  static void pick_hex() {
    card.selectFileByIndex(hex_idx[MenuItemBase::itemIndex]);
    strncpy(hex_dos, card.filename, sizeof(hex_dos) - 1);        hex_dos[sizeof(hex_dos) - 1] = '\0';
    strncpy(hex_long, card.longest_filename(), sizeof(hex_long) - 1); hex_long[sizeof(hex_long) - 1] = '\0';
    show_choice(PSTR("Install firmware from $?"), hex_long, PSTR("Install"), PSTR("Cancel"),
                run_sd_update, []{ ui.goto_screen(menu_sd_hex); }, false);
  }

  static void menu_sd_hex() {
    if (ui.screen_changed) {
      hex_count = 0;
      if (card.isMounted()) {
        card.cdroot();
        const int16_t n = card.get_num_items();
        for (int16_t i = 0; i < n && hex_count < ESC_MAX_HEX; ++i) {
          card.selectFileByIndex(i);
          if (card.flag.filenameIsDir) continue;
          const char * const dot = strrchr(card.filename, '.');
          if (dot && !strcasecmp_P(dot, PSTR(".HEX"))) hex_idx[hex_count++] = i;
        }
      }
    }
    START_MENU();
    ACTION_ITEM_F(GET_TEXT_F(MSG_BACK), []{ ui.goto_screen(menu_escalop_firmware); });
    if (!card.isMounted())  STATIC_ITEM_F(F("Insert the SD card"), SS_LEFT);
    else if (!hex_count)    STATIC_ITEM_F(F("No .hex files"), SS_LEFT);
    for (uint8_t i = 0; i < hex_count; ++i) {
      if (!MY_LINE()) { SKIP_ITEM(); continue; }
      card.selectFileByIndex(hex_idx[i]);
      ACTION_ITEM_N_S_F(i, card.longest_filename(), F("$"), pick_hex);
    }
    END_MENU();
  }

  //////////////////////////////////////////////////////////////////////////////
  // Events from the protocol
  //////////////////////////////////////////////////////////////////////////////

  void on_state() {
    static EscWifi::State last = EscWifi::W_NONE;
    const EscWifi::State s = escWifi.state;
    if (s == EscWifi::W_AP && !ap_shown && millis() - escWifi.boot_ms < 60000UL && !escWifi.is_printing()) {
      ap_shown = ap_from_boot = true;             // the start-up setup page, once
      ui.push_current_screen();
      ui.goto_screen(screen_hotspot);
    }
    else if (s != last && !escWifi.is_printing()) {
      if (s == EscWifi::W_ON)      ui.set_status(F("Wi-Fi connected"));
      else if (s == EscWifi::W_AP) ui.set_status(F("Wi-Fi setup hotspot on"));
    }
    last = s;
    ui.refresh();                                 // the Wi-Fi menu shows the new state
  }

  void on_scan() { ui.refresh(); }

  void on_install() {
    if (ui.currentScreen != screen_installing) ui.goto_screen(screen_installing);
  }

  void on_result(const bool ok, const char * const version) {
    show_message(ok ? PSTR("Firmware updated to $") : PSTR("Firmware update failed - the old firmware is still installed"),
                 version, A_STATUS, 0,
                 escWifi.settings_were_reset ? PSTR("Settings were reset: check the Z offset and level the bed") : nullptr);
  }

  void on_notice(const uint8_t code) {
    PGM_P const t = escWifi.notice_text(code);
    if (!t || ui.currentScreen == screen_checking) return;     // that page shows it itself
    if (escWifi.is_printing()) { ui.set_status(FPSTR(t)); return; }
    show_message(t, nullptr, ui.currentScreen == screen_updating || ui.currentScreen == screen_installing ? A_STATUS : A_PREV, 15);
  }

  bool status_hook() {
    if (!escWifi.offer_pending || escWifi.is_printing()) return false;
    escWifi.offer_pending = false;
    show_offer();
    return true;
  }

  //
  // Drawing from the blocking transfer loops (nothing here prints on the serial port)
  //
  void transfer_progress(PGM_P title, const char * const name, const uint32_t done, const uint32_t total) {
    row_P(0, title);
    row(1, name);
    char line[W + 1];
    if (total) {
      const uint8_t pct = done >= total ? 100 : total > 1000000UL ? done / (total / 100) : done * 100 / total;
      line[0] = '[';
      for (uint8_t i = 0; i < W - 2; ++i) line[i + 1] = i < uint16_t(pct) * (W - 2) / 100 ? '=' : ' ';
      line[W - 1] = ']'; line[W] = '\0';
      row(2, line, false);
      sprintf_P(line, PSTR("%u%%   %lu KB"), pct, (unsigned long)(done / 1024));
    }
    else {
      row(2, nullptr);
      sprintf_P(line, PSTR("%lu KB"), (unsigned long)(done / 1024));
    }
    row(3, line);
  }

  void transfer_done() { ui.refresh(LCDVIEW_CLEAR_CALL_REDRAW); }

} // EscWifiUI

using namespace EscWifiUI;

//////////////////////////////////////////////////////////////////////////////
// Main menu > Wi-Fi
//////////////////////////////////////////////////////////////////////////////

void menu_escalop_wifi() {
  if (ui.screen_changed) escWifi.send(F("STATE"));   // ask the ESP-01 for fresh M9101 / M9107

  const char * const serial = !escWifi.serial[0] ? PSTR("unknown") : escWifi.serial[0] == '-' ? PSTR("not registered") : nullptr;
  char serial_ram[16];
  if (serial) strcpy_P(serial_ram, serial); else strcpy(serial_ram, escWifi.serial);

  START_MENU();
  BACK_ITEM(MSG_MAIN_MENU);
  if (!escWifi.heard)
    STATIC_ITEM_F(F("No Wi-Fi module"), SS_LEFT);
  else switch (escWifi.state) {
    case EscWifi::W_ON:
      STATIC_ITEM_F(F("Connected: "), SS_LEFT, escWifi.ssid);
      STATIC_ITEM_F(F("IP: "), SS_LEFT, escWifi.ip);
      break;
    case EscWifi::W_AP:
      STATIC_ITEM_F(F("Setup hotspot on"), SS_LEFT);
      STATIC_ITEM_F(F(" "), SS_LEFT, escWifi.ap_name);
      break;
    case EscWifi::W_JOIN:
      STATIC_ITEM_F(F("Connecting: "), SS_LEFT, escWifi.ssid);
      break;
    default:
      STATIC_ITEM_F(F("Not connected"), SS_LEFT);
      break;
  }
  ACTION_ITEM_F(F("Choose network"), start_scan);
  ACTION_ITEM_F(F("Set up with phone"), []{
    escWifi.send(F("HOTSPOT"));
    ap_from_boot = false;
    ui.goto_screen(screen_hotspot);
  });
  ACTION_ITEM_F(F("Forget Wi-Fi"), []{
    show_choice(PSTR("Forget the Wi-Fi network? The printer disconnects."), nullptr, PSTR("Forget"), PSTR("Cancel"),
      []{
        escWifi.send(F("RESETWIFI"));
        escWifi.state = EscWifi::W_OFF;
        escWifi.ssid[0] = escWifi.ip[0] = '\0';
        ui.goto_screen(menu_escalop_wifi);
      },
      []{ ui.goto_screen(menu_escalop_wifi); }, false);
  });
  STATIC_ITEM_F(F("Serial: "), SS_LEFT, serial_ram);
  END_MENU();
}

//////////////////////////////////////////////////////////////////////////////
// Main menu > Firmware
//////////////////////////////////////////////////////////////////////////////

void menu_escalop_firmware() {
  START_MENU();
  BACK_ITEM(MSG_MAIN_MENU);
  STATIC_ITEM_F(F("Version " ESCALOPLAB_FW_VERSION), SS_LEFT);
  ACTION_ITEM_F(F("Check for updates"), start_check);
  ACTION_ITEM_F(F("Update from SD card"), []{ ui.goto_screen(menu_sd_hex); });
  END_MENU();
}

#endif // ESCALOPLAB_WIFI && HAS_MARLINUI_MENU
