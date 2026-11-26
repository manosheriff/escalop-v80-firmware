// Escaloplab printer 00003 (the workshop's own test printer)
#include "../variants/220-bowden.h"
#if ESCALOP_VARIANT_STAGE == 1
  // Z 800 (as the standard: 8 mm lead screw, 1.8 degree motor, 1/32); 640 moved 20 mm as 10 mm
  #undef DEFAULT_AXIS_STEPS_PER_UNIT
  #define DEFAULT_AXIS_STEPS_PER_UNIT { 160, 80, 800, 186 }
  #define ESCALOP_FIRMWARE_STEPS   // the 640 saved on this printer must not stay
  #undef ESCALOPLAB_VARIANT
  #define ESCALOPLAB_VARIANT "00003"
  #undef ESCALOPLAB_VARIANT_LABEL
  #define ESCALOPLAB_VARIANT_LABEL " (00003)"
#endif
