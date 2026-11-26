// Escaloplab printer 00003 (the workshop's own test printer)
#include "../variants/220-bowden.h"
#if ESCALOP_VARIANT_STAGE == 1
  // Z 640: a 20 mm cube printed 25 mm tall at 800 (1.7.6) and 20 mm at 640. Short Z at 640 (a 20 mm move gave 10) is lost steps on fast Z moves, not this value
  #undef DEFAULT_AXIS_STEPS_PER_UNIT
  #define DEFAULT_AXIS_STEPS_PER_UNIT { 160, 80, 640, 186 }
  #define ESCALOP_FIRMWARE_STEPS   // the value saved on this printer must not override this one
  #undef ESCALOPLAB_VARIANT
  #define ESCALOPLAB_VARIANT "00003"
  #undef ESCALOPLAB_VARIANT_LABEL
  #define ESCALOPLAB_VARIANT_LABEL " (00003)"
#endif
