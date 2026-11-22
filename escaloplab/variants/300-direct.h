// Escaloplab v80 variant: 300 x 300 bed, direct drive
#include "parts/bed300.h"
#include "parts/direct.h"
#if ESCALOP_VARIANT_STAGE == 1
  #undef ESCALOPLAB_VARIANT
  #define ESCALOPLAB_VARIANT "300-direct"
  #undef ESCALOPLAB_VARIANT_LABEL
  #define ESCALOPLAB_VARIANT_LABEL " (300-direct)"
#endif
