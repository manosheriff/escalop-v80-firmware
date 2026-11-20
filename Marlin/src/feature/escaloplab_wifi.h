/**
 * Escaloplab v80 Wi-Fi (ESP-01 on Serial0)
 *
 * Marlin's side of the ESP-01 protocol, version 1:
 *   workshop/tools/escaloplab/esp01/PROTOCOL.md
 *
 * G-codes M9100-M9108 and M9110 come from the ESP-01; Marlin answers with '//ESC:' lines,
 * and with '#' lines inside the two raw transfers (M9110 upload, SD -> ESP-01 firmware).
 */
#pragma once

#include "../inc/MarlinConfigPre.h"

#if ENABLED(ESCALOPLAB_WIFI)

#define ESC_WIFI_MAX_NETS   10
#define ESC_WIFI_NAME_LEN   20    // Network names are kept only as wide as the LCD
#define ESC_WIFI_PASS_LEN   63

class EscWifi {
public:
  enum State : uint8_t { W_NONE, W_AP, W_JOIN, W_ON, W_OFF, W_BADPW };
  enum Scan  : uint8_t { SCAN_IDLE, SCAN_WAIT, SCAN_LIST, SCAN_DONE };
  enum Stage : uint8_t { UP_NONE, UP_DOWNLOAD, UP_CHECK, UP_INSTALL };

  struct Net { int8_t rssi; bool lock; char name[ESC_WIFI_NAME_LEN + 1]; };

  // What the ESP-01 last told us
  static bool heard;                          // Any M91xx since start-up
  static State state;                         // Last M9101 state
  static uint8_t state_seq;                   // Increments on every M9101
  static char ssid[ESC_WIFI_NAME_LEN + 1],    // Display-safe (ASCII; others become '?')
              ap_name[ESC_WIFI_NAME_LEN + 1],
              ip[16],
              serial[9];                      // "" = unknown, "-" = not registered

  // Scan
  static Scan scan;
  static uint8_t scan_count;
  static uint16_t scan_got;                   // Bit per received index
  static Net nets[ESC_WIFI_MAX_NETS];

  // Firmware update
  static char offer[12];                      // Version offered by M9104, "" = none
  static bool offer_pending;                  // Show the offer on the status screen
  static Stage stage;
  static uint8_t stage_pct;
  static uint8_t stage_seq;                   // Increments on every M9105
  static uint8_t notice_seq;                  // Increments on every M9108
  static uint8_t notice;                      // Last M9108 code (index into the notice table)

  static bool settings_were_reset;            // EEPROM failed to load at start-up (set by settings.cpp)
  static millis_t boot_ms;

  // Called at the end of setup()
  static void boot();

  // G-code entry: codenum 9100..9110, the raw text after the code
  static void gcode(const uint16_t codenum, char *arg);

  // Marlin -> ESP-01 lines
  static void send(FSTR_P const line);        // "//ESC:<line>"
  static void send_info(const bool boot);
  static void send_join(const uint8_t index, const char * const pass);

  // Status of the printer, as the ESP-01 sees it
  static bool is_printing();
  static bool is_busy();

  // Text of a notice code (PROGMEM), nullptr if unknown
  static PGM_P notice_text(const uint8_t code);

  // Firmware update from the SD card (PROTOCOL.md section 10). Blocking. Returns a PROGMEM
  // message to show afterwards, or nullptr after "#VALID" (the ESP-01 resets the board).
  static PGM_P send_sd_firmware(const char * const dosname, const char * const longname);

private:
  static void M9110(char *arg);
};

extern EscWifi escWifi;

#endif // ESCALOPLAB_WIFI
