// Escaloplab v80 part: 300 x 300 bed (printable 290 x 290, the same 10 mm margin as the 220 bed's 210 x 210)
#if ESCALOP_VARIANT_STAGE == 1
  #undef X_BED_SIZE
  #undef Y_BED_SIZE
  #define X_BED_SIZE 290
  #define Y_BED_SIZE 290
#endif
