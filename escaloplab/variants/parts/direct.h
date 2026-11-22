// Escaloplab v80 part: direct-drive extruder (the standard v80 has a Bowden tube).
// Retraction is the slicer's job (Escalop Workshop's "Direct" printer profiles); here only filament changes differ.
#if ESCALOP_VARIANT_STAGE == 2
  #undef FILAMENT_CHANGE_UNLOAD_LENGTH
  #define FILAMENT_CHANGE_UNLOAD_LENGTH 50   // (mm) out of the hotend and the extruder gear; no tube to empty
#endif
