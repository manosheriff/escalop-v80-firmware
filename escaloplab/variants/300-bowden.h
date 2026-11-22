// Escaloplab v80 variant: 300 x 300 bed, Bowden tube
#include "parts/bed300.h"
#if ESCALOP_VARIANT_STAGE == 1
  #undef ESCALOPLAB_VARIANT
  #define ESCALOPLAB_VARIANT "300-bowden"
  #undef ESCALOPLAB_VARIANT_LABEL
  #define ESCALOPLAB_VARIANT_LABEL " (300-bowden)"
#endif
