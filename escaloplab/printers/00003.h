// Escaloplab printer 00003 (the workshop's own test printer)
#include "../variants/220-bowden.h"
#if ESCALOP_VARIANT_STAGE == 1
  // Z 640: 800 printed a 20 mm calibration cube 25 mm tall (800 x 20 / 25)
  #undef DEFAULT_AXIS_STEPS_PER_UNIT
  #define DEFAULT_AXIS_STEPS_PER_UNIT { 160, 80, 640, 186 }
  #undef ESCALOPLAB_VARIANT
  #define ESCALOPLAB_VARIANT "00003"
#endif
