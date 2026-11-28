// Escaloplab printer 00003 (the workshop's own test printer)
#include "../variants/220-bowden.h"
#if ESCALOP_VARIANT_STAGE == 1
  // 1.8.0: like every v80, all A4988 at 1/16 (X 80, Y 80, Z 400, E 93). The standard build already makes the
  // firmware's steps beat the saved ones, so this file only names the printer.
  #undef ESCALOPLAB_VARIANT
  #define ESCALOPLAB_VARIANT "00003"
  #undef ESCALOPLAB_VARIANT_LABEL
  #define ESCALOPLAB_VARIANT_LABEL " (00003)"
#endif
