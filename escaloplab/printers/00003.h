// Escaloplab printer 00003 (the workshop's own test printer)
#include "../variants/220-bowden.h"
#if ESCALOP_VARIANT_STAGE == 1
  // 2026-11-26: the Z DRV8825 was faulty (lost steps: 25 mm cubes at 800). The X driver moved to Z; X now has an
  // A4988 with all 3 jumpers (1/16), so X 80. Z stays at the standard 800.
  #undef X_DRIVER_TYPE
  #define X_DRIVER_TYPE A4988
  #undef DEFAULT_AXIS_STEPS_PER_UNIT
  #define DEFAULT_AXIS_STEPS_PER_UNIT { 80, 80, 800, 186 }
  #define ESCALOP_FIRMWARE_STEPS   // the steps saved on this printer must not override these
  #undef ESCALOPLAB_MICROSTEP_INFO
  #define ESCALOPLAB_MICROSTEP_INFO "X+Y 1/16  Z+E 1/32"
  #undef ESCALOPLAB_VARIANT
  #define ESCALOPLAB_VARIANT "00003"
  #undef ESCALOPLAB_VARIANT_LABEL
  #define ESCALOPLAB_VARIANT_LABEL " (00003)"
#endif
