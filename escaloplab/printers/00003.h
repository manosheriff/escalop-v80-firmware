// Escaloplab printer 00003 (the workshop's own test printer)
#include "../variants/220-bowden.h"
#if ESCALOP_VARIANT_STAGE == 1
  // 2026-11-26: the Z DRV8825 was faulty (lost steps: 25 mm cubes at 800) and was replaced. Same drivers and
  // steps as the standard v80 (X 160, Z 800); this file only makes them beat the steps saved on the printer.
  #define ESCALOP_FIRMWARE_STEPS
  #undef ESCALOPLAB_VARIANT
  #define ESCALOPLAB_VARIANT "00003"
  #undef ESCALOPLAB_VARIANT_LABEL
  #define ESCALOPLAB_VARIANT_LABEL " (00003)"
#endif
