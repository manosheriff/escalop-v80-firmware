/**
 * Escaloplab v80 Wi-Fi: LCD screens (20x4)
 */
#pragma once

#include "../../inc/MarlinConfigPre.h"

#if ENABLED(ESCALOPLAB_WIFI)

namespace EscWifiUI {
  // Events from the protocol (feature/escaloplab_wifi.cpp)
  void on_state();                                  // M9101
  void on_scan();                                   // M9102 / M9103
  void on_install();                                // M9105 INSTALL, or '#VALID' after an SD update
  void on_result(const bool ok, const char * const version);   // M9106
  void on_notice(const uint8_t code);               // M9108

  // Drawing from inside the blocking transfer loops
  void transfer_progress(PGM_P title, const char * const name, const uint32_t done, const uint32_t total);
  void transfer_done();
  void draw_installing();

  // Called by the status screen. Returns true when it switched to another screen.
  bool status_hook();
}

void menu_escalop_wifi();
void menu_escalop_firmware();

#endif
