// Escaloplab v80 variant: 220 x 220 bed, direct drive
#include "parts/direct.h"
#if ESCALOP_VARIANT_STAGE == 1
  #undef ESCALOPLAB_VARIANT
  #define ESCALOPLAB_VARIANT "220-direct"
  #undef ESCALOPLAB_VARIANT_LABEL
  #define ESCALOPLAB_VARIANT_LABEL " (220-direct)"
#endif
