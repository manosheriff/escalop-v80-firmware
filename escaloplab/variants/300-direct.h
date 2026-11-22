// Escaloplab v80 variant: 300 x 300 bed, direct drive
#include "parts/bed300.h"
#include "parts/direct.h"
#if ESCALOP_VARIANT_STAGE == 1
  #undef ESCALOPLAB_VARIANT
  #define ESCALOPLAB_VARIANT "300-direct"
#endif
