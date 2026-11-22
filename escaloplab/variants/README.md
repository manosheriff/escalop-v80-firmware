# Escaloplab v80 printer variants

One Marlin source for every v80. A **variant** changes only what differs (bed size, Bowden or direct drive, ...);
a **printer file** (`escaloplab/printers/<serial>.h`) is for one printer and starts from a variant.

Every release is built for all of them by the workshop repo's GitHub Actions workflow
"v80 printer firmware" (`.github/workflows/escalop_firmware.yml` in manosheriff/escalop-workshop):

| Build                     | Release asset                                  |
|---------------------------|------------------------------------------------|
| standard (no file)        | `EscalopV80_Firmware_<version>.hex`            |
| `variants/<name>.h`       | `EscalopV80_Firmware_<version>_<name>.hex`     |
| `printers/<serial>.h`     | `EscalopV80_Firmware_<version>_<serial>.hex`   |

The printer's Wi-Fi module installs, in this order: the file for its own serial number if there is one, else the
variant escaloplab.com assigns to it (Admin > Printer units), else the variant it is running now, else standard.

## Writing a variant

Names: lowercase letters, digits and `-`, at most 16 characters. The file is included twice: at the end of
`Configuration.h` (`ESCALOP_VARIANT_STAGE == 1`) and at the end of `Configuration_adv.h` (stage 2). Replace a setting
with `#undef` + `#define`. Set `ESCALOPLAB_VARIANT` and `ESCALOPLAB_VARIANT_LABEL` last, after any `#include`:

```c
#include "parts/bed300.h"
#if ESCALOP_VARIANT_STAGE == 1
  #undef ESCALOPLAB_VARIANT
  #define ESCALOPLAB_VARIANT "300-bowden"
  #undef ESCALOPLAB_VARIANT_LABEL
  #define ESCALOPLAB_VARIANT_LABEL " (300-bowden)"   // shown by M115
#endif
```

Build them all locally: `python escaloplab/build_variants.py` (or name some: `... 300-direct 00003`). The files land
in `escaloplab/out/`.

Remember the slicer: a printer with another bed size or drive needs the matching Escalop Workshop printer profile.
