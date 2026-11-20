/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

//
// Info Menu
//

#include "../../inc/MarlinConfigPre.h"

#if ALL(HAS_MARLINUI_MENU, LCD_INFO_MENU)

#include "menu_item.h"

#if HAS_GAMES
  #include "game/game.h"
#endif

#if ALL(ESCALOPLAB_INFO_SCREEN, HAS_MARLINUI_HD44780) && LCD_WIDTH >= 20 && LCD_HEIGHT >= 4

/**
 * Escaloplab Info screen (20x4)
 *
 * One full-screen deck of cards in place of the stock "About Printer" menus.
 * Turn the dial to flip cards, click to leave. Each new card is laid down
 * left to right in four quick passes, like the wordmark on the boot screen.
 *
 *   Cover   |    3D PRINTERS     |    Live     |N LIVE          o*ooo|
 *           |[ E S C A L O P ] |              |h Hotend     205/210d|
 *           |[ wordmark L A B ]|              |b Bed          60/60d|
 *           |   escaloplab.com  >|              |c Powered on    12'34|
 *
 *   Machine |N MACHINE       oo*oo|  Motion |N STEPS / MM     ooo*o|
 *           |  Build   210x210x200|          |  X   80.0   Y   80.0|
 *           |  Hotend       E3D V6|          |  Z  800.0   E  186.0|
 *           |  Bed      MK2B + PEI|          |  XY 1/16   Z+E 1/32 |
 *
 *   Firmware|N FIRMWARE      oooo*|
 *           |  Escaloplab     v1.6|   N = logo, o and * = page dots,
 *           |  Marlin      2.1.2.9|   h/b/c = hotend / bed / clock icons
 *           |  Board     RAMPS 1.4|
 */

#include "../../module/temperature.h"
#include "../../module/planner.h"
#include "../../libs/duration_t.h"
#include "../../libs/numtostr.h"
#if ENABLED(ESCALOPLAB_WIFI)
  #include "../../feature/escaloplab_wifi.h"
#endif

void esc_lcd_glyph(const uint8_t slot, const uint8_t * const pgm);  // marlinui_HD44780.cpp
bool esc_lcd_wordmark(char * const top, char * const bot);

namespace EscInfo {

  // Content-card glyph slots. Slot 0 is only ever placed straight into a cell, never in a string.
  enum : uint8_t { G_LOGO, G_DEG, G_ON, G_OFF, G_NOZ, G_BED, G_CLOCK, G_WARN };

  static const PROGMEM uint8_t glyphs[8][8] = {
    { B00100, B01110, B11011, B10001, B00100, B01110, B11011, B10001 },   // logo: stacked chevrons
    { B01100, B10010, B10010, B01100, B00000, B00000, B00000, B00000 },   // degree
    { B00000, B01110, B11111, B11111, B11111, B01110, B00000, B00000 },   // page marker, current
    { B00000, B00000, B00000, B01110, B00000, B00000, B00000, B00000 },   // page marker, other
    { B11111, B11111, B01110, B11111, B11111, B01110, B00100, B00000 },   // hotend
    { B01001, B10010, B01001, B00000, B11111, B11111, B10001, B00000 },   // bed
    { B00000, B01110, B10011, B10101, B10001, B01110, B00000, B00000 },   // clock
    { B00100, B01110, B01010, B11011, B11111, B11011, B11111, B00000 }    // warning: sensor missing
  };

  enum : uint8_t { P_COVER, P_LIVE, P_MACHINE, P_MOTION,
    #if ENABLED(ESCALOPLAB_WIFI)
      P_WIFI,
    #endif
    P_FIRMWARE, PAGES };
  constexpr uint8_t W = LCD_WIDTH, SWEEP = 4;   // a new card is laid down in 4 passes of 5 columns

  static char cell[4][W];
  static uint8_t page, reveal, glyph_set, redraw_wait;   // glyph_set: 0 = none, 1 = wordmark, 2 = content

  static void put(uint8_t c, const uint8_t r, const char *s) { while (*s && c < W) cell[r][c++] = *s++; }
  static void put_P(uint8_t c, const uint8_t r, PGM_P p) { for (char ch; (ch = pgm_read_byte(p)) && c < W; ++p) cell[r][c++] = ch; }
  static void put_right(const uint8_t r, const char *s, const uint8_t end=W) { const uint8_t n = strlen(s); put(end > n ? end - n : 0, r, s); }

  // "  Label         value" rows: label from column 2, value flush right
  static void row(const uint8_t r, PGM_P label, const char *value) { put_P(2, r, label); put_right(r, value); }
  static void row_P(const uint8_t r, PGM_P label, PGM_P value) { char buf[W + 1]; strncpy_P(buf, value, W); buf[W] = '\0'; row(r, label, buf); }

  static void header(PGM_P title) {
    cell[0][0] = char(G_LOGO);
    put_P(2, 0, title);
    for (uint8_t i = 0; i < PAGES; ++i) cell[0][W - PAGES + i] = char(i == page ? G_ON : G_OFF);
  }

  // One decimal place, e.g. 186.0
  static void fmt1(char * const buf, const float v) {
    const long t = lroundf(v * 10);
    sprintf_P(buf, PSTR("%ld.%ld"), t / 10, t % 10);
  }

  // "205/210°", "23° off", or a sensor fault
  static bool heater(char * const buf, const celsius_t now, const celsius_t target) {
    if (now < 0) { strcpy_P(buf, PSTR("NO SENSOR")); return false; }
    if (target > 0) sprintf_P(buf, PSTR("%d/%d" LCD_STR_DEGREE), int(now), int(target));
    else            sprintf_P(buf, PSTR("%d" LCD_STR_DEGREE " off"), int(now));
    return true;
  }

  static void build() {
    memset(cell, ' ', sizeof(cell));
    char buf[W + 1];

    switch (page) {
      case P_COVER: {
        put_P((W - 11) / 2, 0, PSTR("3D PRINTERS"));
        if (!esc_lcd_wordmark(cell[1], cell[2])) put_P((W - 19) / 2, 1, PSTR("E S C A L O P L A B"));
        put_P((W - (sizeof(WEBSITE_URL) - 1)) / 2, 3, PSTR(WEBSITE_URL));
        cell[3][W - 1] = '>';                       // turn the dial for more
      } break;

      case P_LIVE: {
        header(PSTR("LIVE"));
        const bool blink = ui.get_blink();
        bool ok = heater(buf, thermalManager.wholeDegHotend(0), thermalManager.degTargetHotend(0));
        cell[1][0] = char(ok || blink ? G_NOZ : G_WARN);
        put_P(2, 1, PSTR("Hotend")); put_right(1, buf);
        #if HAS_HEATED_BED
          ok = heater(buf, thermalManager.wholeDegBed(), thermalManager.degTargetBed());
          cell[2][0] = char(ok || blink ? G_BED : G_WARN);
          put_P(2, 2, PSTR("Bed")); put_right(2, buf);
        #endif
        const duration_t up = millis() / 1000UL;
        up.toDigital(buf, up.day() > 0);
        cell[3][0] = char(G_CLOCK);
        put_P(2, 3, PSTR("Powered on")); put_right(3, buf);
      } break;

      case P_MACHINE:
        header(PSTR("MACHINE"));
        row_P(1, PSTR("Build"),  PSTR(STRINGIFY(X_BED_SIZE) "x" STRINGIFY(Y_BED_SIZE) "x" STRINGIFY(Z_MAX_POS)));
        row_P(2, PSTR("Hotend"), PSTR("E3D V6"));
        row_P(3, PSTR("Bed"),    PSTR("MK2B + PEI"));
        break;

      case P_MOTION: {
        header(PSTR("STEPS / MM"));
        const float * const spm = planner.settings.axis_steps_per_mm;
        cell[1][2] = 'X'; fmt1(buf, spm[X_AXIS]);      put_right(1, buf, 9);
        cell[1][12] = 'Y'; fmt1(buf, spm[Y_AXIS]);     put_right(1, buf, W);
        cell[2][2] = 'Z'; fmt1(buf, spm[Z_AXIS]);      put_right(2, buf, 9);
        cell[2][12] = 'E'; fmt1(buf, spm[E_AXIS]);      put_right(2, buf, W);
        #ifdef ESCALOPLAB_MICROSTEP_INFO
          put_P(2, 3, PSTR(ESCALOPLAB_MICROSTEP_INFO));
        #endif
      } break;

      #if ENABLED(ESCALOPLAB_WIFI)
        case P_WIFI: {
          header(PSTR("WI-FI"));
          PGM_P st;
          if (!escWifi.heard) st = PSTR("No module");
          else switch (escWifi.state) {
            case EscWifi::W_ON:   st = PSTR("Connected");     break;
            case EscWifi::W_AP:   st = PSTR("Setup hotspot"); break;
            case EscWifi::W_JOIN: st = PSTR("Connecting");    break;
            default:              st = PSTR("Not connected"); break;
          }
          row_P(1, PSTR("Wi-Fi"), st);
          if (escWifi.state == EscWifi::W_ON && escWifi.ip[0]) row(2, PSTR("IP"), escWifi.ip);
          else if (escWifi.state == EscWifi::W_AP && escWifi.ap_name[0]) row(2, PSTR("Join"), escWifi.ap_name);
          if (!escWifi.serial[0])           row_P(3, PSTR("Serial"), PSTR("unknown"));
          else if (escWifi.serial[0] == '-') row_P(3, PSTR("Serial"), PSTR("not registered"));
          else                               row(3, PSTR("Serial"), escWifi.serial);
        } break;
      #endif

      case P_FIRMWARE:
        header(PSTR("FIRMWARE"));
        row_P(1, PSTR("Escaloplab"), PSTR("v" ESCALOPLAB_FW_VERSION));
        row_P(2, PSTR("Marlin"),     PSTR(SHORT_BUILD_VERSION));   // credit to the firmware this is built on
        row_P(3, PSTR("Board"),      PSTR(BOARD_INFO_NAME));
        break;
    }
  }

  // Draw columns [from, to) of every row
  static void draw(const uint8_t from, const uint8_t to) {
    for (uint8_t r = 0; r < 4; ++r) {
      lcd_moveto(from, r);
      for (uint8_t c = from; c < to; ++c) lcd_put_lchar(lchar_t(uint8_t(cell[r][c])));
    }
  }

  static void load_glyphs() {
    const uint8_t want = page == P_COVER ? 1 : 2;
    if (want == glyph_set) return;
    glyph_set = want;
    if (want == 2) for (uint8_t i = 0; i < 8; ++i) esc_lcd_glyph(i, glyphs[i]);
    // The cover's wordmark glyphs are loaded by build()
  }

  void screen() {
    ui.encoder_direction_menus();
    ENCODER_RATE_MULTIPLY(false);

    if (ui.use_click()) return ui.go_back();          // leaving restores the menu glyphs

    if (ui.screen_changed) {                          // just arrived
      ui.screen_changed = false;
      ui.encoderPosition = 0;
      page = 0xFF; glyph_set = 0;
    }

    // Dial -> page, held within the deck
    if (int32_t(ui.encoderPosition) < 0) ui.encoderPosition = 0;
    uint8_t want = ui.encoderPosition / (ENCODER_STEPS_PER_MENU_ITEM);
    if (want >= PAGES) { want = PAGES - 1; ui.encoderPosition = want * (ENCODER_STEPS_PER_MENU_ITEM); }

    if (want != page) {                               // flip to a new card
      page = want;
      reveal = 0;
      if (page == P_COVER) glyph_set = 1; else load_glyphs();
      ui.clear_lcd();
    }

    if (reveal < SWEEP) {                             // lay the card down, 5 columns per pass
      build();
      const uint8_t step = W / SWEEP;
      draw(reveal * step, (reveal + 1) * step);
      if (++reveal == SWEEP) redraw_wait = 5;
    }
    else if (!redraw_wait--) {                        // then keep live values fresh (~2x per second)
      redraw_wait = 5;
      if (page == P_LIVE) { build(); draw(0, W); }
    }

    ui.refresh(LCDVIEW_CALL_NO_REDRAW);               // call back on every LCD update for the animation
  }

} // EscInfo

void menu_info() { EscInfo::screen(); }

#else // !ESCALOPLAB_INFO_SCREEN

#define VALUE_ITEM(MSG, VALUE, STYL)    do{ char msg[21]; strcpy_P(msg, PSTR(": ")); strcpy(msg + 2, VALUE); STATIC_ITEM(MSG, STYL, msg); }while(0)
#define VALUE_ITEM_F(MSG, PVALUE, STYL) do{ char msg[21]; strcpy_P(msg, PSTR(": ")); strcpy_P(msg + 2, PSTR(PVALUE)); STATIC_ITEM(MSG, STYL, msg); }while(0)

#if ENABLED(PRINTCOUNTER)

  #include "../../module/printcounter.h"

  //
  // About Printer > Printer Stats
  //
  void menu_info_stats() {
    if (ui.use_click()) return ui.go_back();

    printStatistics stats = print_job_timer.getStats();

    char buffer[21];

    START_SCREEN();                                                                         // 12345678901234567890
    VALUE_ITEM(MSG_INFO_PRINT_COUNT, i16tostr3left(stats.totalPrints), SS_LEFT);            // Print Count: 999
    VALUE_ITEM(MSG_INFO_COMPLETED_PRINTS, i16tostr3left(stats.finishedPrints), SS_LEFT);    // Completed  : 666

    STATIC_ITEM(MSG_INFO_PRINT_TIME, SS_LEFT);                                              // Total print Time:
    STATIC_ITEM_F(F("> "), SS_LEFT, duration_t(stats.printTime).toString(buffer));       // > 99y 364d 23h 59m 59s

    STATIC_ITEM(MSG_INFO_PRINT_LONGEST, SS_LEFT);                                           // Longest job time:
    STATIC_ITEM_F(F("> "), SS_LEFT, duration_t(stats.longestPrint).toString(buffer));    // > 99y 364d 23h 59m 59s

    STATIC_ITEM(MSG_INFO_PRINT_FILAMENT, SS_LEFT);                                          // Extruded total:
    sprintf_P(buffer, PSTR("%ld.%im")
      , long(stats.filamentUsed / 1000)
      , int16_t(stats.filamentUsed / 100) % 10
    );
    STATIC_ITEM_F(F("> "), SS_LEFT, buffer);                                             // > 125m

    #if SERVICE_INTERVAL_1 > 0 || SERVICE_INTERVAL_2 > 0 || SERVICE_INTERVAL_3 > 0
      strcpy_P(buffer, GET_TEXT(MSG_SERVICE_IN));
    #endif

    #if SERVICE_INTERVAL_1 > 0
      STATIC_ITEM_F(F(SERVICE_NAME_1 " "), SS_LEFT, buffer);                             // Service X in:
      STATIC_ITEM_F(F("> "), SS_LEFT, duration_t(stats.nextService1).toString(buffer));  // > 7d 12h 11m 10s
    #endif

    #if SERVICE_INTERVAL_2 > 0
      STATIC_ITEM_F(F(SERVICE_NAME_2 " "), SS_LEFT, buffer);
      STATIC_ITEM_F(F("> "), SS_LEFT, duration_t(stats.nextService2).toString(buffer));
    #endif

    #if SERVICE_INTERVAL_3 > 0
      STATIC_ITEM_F(F(SERVICE_NAME_3 " "), SS_LEFT, buffer);
      STATIC_ITEM_F(F("> "), SS_LEFT, duration_t(stats.nextService3).toString(buffer));
    #endif

    END_SCREEN();
  }

#endif

//
// About Printer > Thermistors
//
void menu_info_thermistors() {
  if (ui.use_click()) return ui.go_back();

  START_SCREEN();

  #if HAS_EXTRUDERS
    #define THERMISTOR_ID TEMP_SENSOR_0
    #include "../thermistornames.h"
    STATIC_ITEM_F(F(STR_E0 ": " THERMISTOR_NAME), SS_INVERT);
    PSTRING_ITEM(MSG_INFO_MIN_TEMP, STRINGIFY(HEATER_0_MINTEMP), SS_LEFT);
    PSTRING_ITEM(MSG_INFO_MAX_TEMP, STRINGIFY(HEATER_0_MAXTEMP), SS_LEFT);
    STATIC_ITEM(TERN(WATCH_HOTENDS, MSG_INFO_RUNAWAY_ON, MSG_INFO_RUNAWAY_OFF), SS_LEFT);
  #endif

  #if TEMP_SENSOR_1 != 0
    #undef THERMISTOR_ID
    #define THERMISTOR_ID TEMP_SENSOR_1
    #include "../thermistornames.h"
    STATIC_ITEM_F(F(STR_E1 ": " THERMISTOR_NAME), SS_INVERT);
    PSTRING_ITEM(MSG_INFO_MIN_TEMP, STRINGIFY(HEATER_1_MINTEMP), SS_LEFT);
    PSTRING_ITEM(MSG_INFO_MAX_TEMP, STRINGIFY(HEATER_1_MAXTEMP), SS_LEFT);
    STATIC_ITEM(TERN(WATCH_HOTENDS, MSG_INFO_RUNAWAY_ON, MSG_INFO_RUNAWAY_OFF), SS_LEFT);
  #endif

  #if TEMP_SENSOR_2 != 0
    #undef THERMISTOR_ID
    #define THERMISTOR_ID TEMP_SENSOR_2
    #include "../thermistornames.h"
    STATIC_ITEM_F(F(STR_E2 ": " THERMISTOR_NAME), SS_INVERT);
    PSTRING_ITEM(MSG_INFO_MIN_TEMP, STRINGIFY(HEATER_2_MINTEMP), SS_LEFT);
    PSTRING_ITEM(MSG_INFO_MAX_TEMP, STRINGIFY(HEATER_2_MAXTEMP), SS_LEFT);
    STATIC_ITEM(TERN(WATCH_HOTENDS, MSG_INFO_RUNAWAY_ON, MSG_INFO_RUNAWAY_OFF), SS_LEFT);
  #endif

  #if TEMP_SENSOR_3 != 0
    #undef THERMISTOR_ID
    #define THERMISTOR_ID TEMP_SENSOR_3
    #include "../thermistornames.h"
    STATIC_ITEM_F(F(STR_E3 ": " THERMISTOR_NAME), SS_INVERT);
    PSTRING_ITEM(MSG_INFO_MIN_TEMP, STRINGIFY(HEATER_3_MINTEMP), SS_LEFT);
    PSTRING_ITEM(MSG_INFO_MAX_TEMP, STRINGIFY(HEATER_3_MAXTEMP), SS_LEFT);
    STATIC_ITEM(TERN(WATCH_HOTENDS, MSG_INFO_RUNAWAY_ON, MSG_INFO_RUNAWAY_OFF), SS_LEFT);
  #endif

  #if TEMP_SENSOR_4 != 0
    #undef THERMISTOR_ID
    #define THERMISTOR_ID TEMP_SENSOR_4
    #include "../thermistornames.h"
    STATIC_ITEM_F(F(STR_E4 ": " THERMISTOR_NAME), SS_INVERT);
    PSTRING_ITEM(MSG_INFO_MIN_TEMP, STRINGIFY(HEATER_4_MINTEMP), SS_LEFT);
    PSTRING_ITEM(MSG_INFO_MAX_TEMP, STRINGIFY(HEATER_4_MAXTEMP), SS_LEFT);
    STATIC_ITEM(TERN(WATCH_HOTENDS, MSG_INFO_RUNAWAY_ON, MSG_INFO_RUNAWAY_OFF), SS_LEFT);
  #endif

  #if TEMP_SENSOR_5 != 0
    #undef THERMISTOR_ID
    #define THERMISTOR_ID TEMP_SENSOR_5
    #include "../thermistornames.h"
    STATIC_ITEM_F(F(STR_E5 ": " THERMISTOR_NAME), SS_INVERT);
    PSTRING_ITEM(MSG_INFO_MIN_TEMP, STRINGIFY(HEATER_5_MINTEMP), SS_LEFT);
    PSTRING_ITEM(MSG_INFO_MAX_TEMP, STRINGIFY(HEATER_5_MAXTEMP), SS_LEFT);
    STATIC_ITEM(TERN(WATCH_HOTENDS, MSG_INFO_RUNAWAY_ON, MSG_INFO_RUNAWAY_OFF), SS_LEFT);
  #endif

  #if TEMP_SENSOR_6 != 0
    #undef THERMISTOR_ID
    #define THERMISTOR_ID TEMP_SENSOR_6
    #include "../thermistornames.h"
    STATIC_ITEM_F(F(STR_E6 ": " THERMISTOR_NAME), SS_INVERT);
    PSTRING_ITEM(MSG_INFO_MIN_TEMP, STRINGIFY(HEATER_6_MINTEMP), SS_LEFT);
    PSTRING_ITEM(MSG_INFO_MAX_TEMP, STRINGIFY(HEATER_6_MAXTEMP), SS_LEFT);
    STATIC_ITEM(TERN(WATCH_HOTENDS, MSG_INFO_RUNAWAY_ON, MSG_INFO_RUNAWAY_OFF), SS_LEFT);
  #endif

  #if TEMP_SENSOR_7 != 0
    #undef THERMISTOR_ID
    #define THERMISTOR_ID TEMP_SENSOR_7
    #include "../thermistornames.h"
    STATIC_ITEM_F(F(STR_E7 ": " THERMISTOR_NAME), SS_INVERT);
    PSTRING_ITEM(MSG_INFO_MIN_TEMP, STRINGIFY(HEATER_7_MINTEMP), SS_LEFT);
    PSTRING_ITEM(MSG_INFO_MAX_TEMP, STRINGIFY(HEATER_7_MAXTEMP), SS_LEFT);
    STATIC_ITEM(TERN(WATCH_HOTENDS, MSG_INFO_RUNAWAY_ON, MSG_INFO_RUNAWAY_OFF), SS_LEFT);
  #endif

  #if HAS_HEATED_BED
    #undef THERMISTOR_ID
    #define THERMISTOR_ID TEMP_SENSOR_BED
    #include "../thermistornames.h"
    STATIC_ITEM_F(F("BED: " THERMISTOR_NAME), SS_INVERT);
    PSTRING_ITEM(MSG_INFO_MIN_TEMP, STRINGIFY(BED_MINTEMP), SS_LEFT);
    PSTRING_ITEM(MSG_INFO_MAX_TEMP, STRINGIFY(BED_MAXTEMP), SS_LEFT);
    STATIC_ITEM(TERN(WATCH_BED, MSG_INFO_RUNAWAY_ON, MSG_INFO_RUNAWAY_OFF), SS_LEFT);
  #endif

  #if HAS_HEATED_CHAMBER
    #undef THERMISTOR_ID
    #define THERMISTOR_ID TEMP_SENSOR_CHAMBER
    #include "../thermistornames.h"
    STATIC_ITEM_F(F("CHAM: " THERMISTOR_NAME), SS_INVERT);
    PSTRING_ITEM(MSG_INFO_MIN_TEMP, STRINGIFY(CHAMBER_MINTEMP), SS_LEFT);
    PSTRING_ITEM(MSG_INFO_MAX_TEMP, STRINGIFY(CHAMBER_MAXTEMP), SS_LEFT);
    STATIC_ITEM(TERN(WATCH_CHAMBER, MSG_INFO_RUNAWAY_ON, MSG_INFO_RUNAWAY_OFF), SS_LEFT);
  #endif

  #if HAS_COOLER
    #undef THERMISTOR_ID
    #define THERMISTOR_ID TEMP_SENSOR_COOLER
    #include "../thermistornames.h"
    STATIC_ITEM_F(F("COOL: " THERMISTOR_NAME), SS_INVERT);
    PSTRING_ITEM(MSG_INFO_MIN_TEMP, STRINGIFY(COOLER_MINTEMP), SS_LEFT);
    PSTRING_ITEM(MSG_INFO_MAX_TEMP, STRINGIFY(COOLER_MAXTEMP), SS_LEFT);
    STATIC_ITEM(TERN(WATCH_COOLER, MSG_INFO_RUNAWAY_ON, MSG_INFO_RUNAWAY_OFF), SS_LEFT);
  #endif

  END_SCREEN();
}

//
// About Printer > Board Info
//
void menu_info_board() {
  if (ui.use_click()) return ui.go_back();

  START_SCREEN();
  STATIC_ITEM_F(F(BOARD_INFO_NAME), SS_DEFAULT|SS_INVERT);      // MyPrinterController
  #ifdef BOARD_WEBSITE_URL
    STATIC_ITEM_F(F(BOARD_WEBSITE_URL), SS_LEFT);               // www.my3dprinter.com
  #endif
  PSTRING_ITEM(MSG_INFO_BAUDRATE, STRINGIFY(BAUDRATE), SS_CENTER); // Baud: 250000
  PSTRING_ITEM(MSG_INFO_PROTOCOL, PROTOCOL_VERSION, SS_CENTER);    // Protocol: 1.0
  PSTRING_ITEM(MSG_INFO_PSU, PSU_NAME, SS_CENTER);
  END_SCREEN();
}

//
// About Printer > Printer Info
//
#if ENABLED(LCD_PRINTER_INFO_IS_BOOTSCREEN)

  void menu_show_marlin_bootscreen() {
    if (ui.use_click()) { ui.goto_previous_screen_no_defer(); }
    ui.draw_marlin_bootscreen();
  }

  #if ENABLED(SHOW_CUSTOM_BOOTSCREEN)
    void menu_show_custom_bootscreen() {
      if (ui.use_click()) { ui.goto_screen(menu_show_marlin_bootscreen); }
      ui.draw_custom_bootscreen();
    }
  #endif

#else

  void menu_info_printer() {
    if (ui.use_click()) return ui.go_back();
    START_SCREEN();
    STATIC_ITEM(MSG_MARLIN, SS_DEFAULT|SS_INVERT);                // Marlin
    STATIC_ITEM_F(F(SHORT_BUILD_VERSION));                        // x.x.x-Branch
    STATIC_ITEM_F(F(STRING_DISTRIBUTION_DATE));                   // YYYY-MM-DD HH:MM
    STATIC_ITEM_F(F(MACHINE_NAME), SS_DEFAULT|SS_INVERT);         // My3DPrinter
    STATIC_ITEM_F(F(WEBSITE_URL));                                // www.my3dprinter.com
    PSTRING_ITEM(MSG_INFO_EXTRUDERS, STRINGIFY(EXTRUDERS), SS_CENTER); // Extruders: 2
    #if HAS_LEVELING
      STATIC_ITEM(
        TERN_(AUTO_BED_LEVELING_3POINT, MSG_3POINT_LEVELING)      // 3-Point Leveling
        TERN_(AUTO_BED_LEVELING_LINEAR, MSG_LINEAR_LEVELING)      // Linear Leveling
        TERN_(AUTO_BED_LEVELING_BILINEAR, MSG_BILINEAR_LEVELING)  // Bi-linear Leveling
        TERN_(AUTO_BED_LEVELING_UBL, MSG_UBL_LEVELING)            // Unified Bed Leveling
        TERN_(MESH_BED_LEVELING, MSG_MESH_LEVELING)               // Mesh Leveling
      );
    #endif
    END_SCREEN();
  }

#endif

//
// "About Printer" submenu
//
void menu_info() {
  START_MENU();
  BACK_ITEM(MSG_MAIN_MENU);
  #if ENABLED(LCD_PRINTER_INFO_IS_BOOTSCREEN)
    SUBMENU(MSG_INFO_PRINTER_MENU, TERN(SHOW_CUSTOM_BOOTSCREEN, menu_show_custom_bootscreen, menu_show_marlin_bootscreen));
  #else
    SUBMENU(MSG_INFO_PRINTER_MENU, menu_info_printer);           // Printer Info >
    SUBMENU(MSG_INFO_BOARD_MENU, menu_info_board);               // Board Info >
    #if HAS_EXTRUDERS
      SUBMENU(MSG_INFO_THERMISTOR_MENU, menu_info_thermistors);  // Thermistors >
    #endif
  #endif

  #if ENABLED(PRINTCOUNTER)
    SUBMENU(MSG_INFO_STATS_MENU, menu_info_stats);               // Printer Stats >
  #endif

  #if HAS_GAMES
  {
    #if ENABLED(GAMES_EASTER_EGG)
      SKIP_ITEM(); SKIP_ITEM(); SKIP_ITEM();
    #endif

    // Game sub-menu or the individual game
    SUBMENU(
      #if HAS_GAME_MENU
        MSG_GAMES, menu_game
      #elif ENABLED(MARLIN_BRICKOUT)
        MSG_BRICKOUT, brickout.enter_game
      #elif ENABLED(MARLIN_INVADERS)
        MSG_INVADERS, invaders.enter_game
      #elif ENABLED(MARLIN_SNAKE)
        MSG_SNAKE, snake.enter_game
      #elif ENABLED(MARLIN_MAZE)
        MSG_MAZE, maze.enter_game
      #endif
    );
  }
  #endif

  END_MENU();
}

#endif // !ESCALOPLAB_INFO_SCREEN

#endif // HAS_MARLINUI_MENU && LCD_INFO_MENU
