# CLATTER 12: copyright, licence and trademark notice

Copyright (C) 2026 The CLATTER contributors.

CLATTER 12 is free software: you can redistribute it and/or modify it under the terms of the
**GNU General Public License version 3 only** (GPL-3.0-only), with the additional terms below. The
licence text is in [LICENSE](LICENSE). It is distributed WITHOUT ANY WARRANTY; without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

## The LXR firmware

CLATTER 12 contains and is a modified version of parts of the Erica Synths LXR-02 firmware
(<https://github.com/erica-synths/lxr-desktop-app>), Copyright (C) 2012-2026 Erica Synths, licensed
under GPL-3.0-only with additional terms under section 7, taken through CLATTER
(<https://github.com/clickysteve/clatter>) and CLATTER Pocket
(<https://github.com/clickysteve/clatter-pocket>, the pinned submodule `third_party/clatter-pocket`,
where the firmware files are kept unchanged with their own LICENSE and NOTICE.md). The firmware's
additional terms apply to CLATTER 12 as a whole:

1. **Trademarks (section 7(e)).** No rights in the names "Erica Synths", "LXR" and "LXR-02" or the
   associated logos. CLATTER 12 uses them only to say where its synthesis comes from, uses no Erica
   Synths logo, and is **not** made, approved, endorsed or supported by Erica Synths.
2. **Marking of modified versions (section 7(c)).** CLATTER 12 is a modified version of the
   firmware's sound engine and says so, in its documentation and in the header of every generated
   panel file. Modified versions must be marked as such and not presented as official Erica Synths
   software.

The generated panel source (`build/clatter_12*.cpp`) is a derived work of all of the above, and
carries this notice in short form.

## Plinky 12

"Plinky" and "Plinky 12" name the target hardware only. CLATTER 12 is not a Plinky product. It uses
Plinky's published panel API and contains none of Plinky's code: the headers are injected by
Plinky's IDE at build time, and the host simulator (`sim/`) is an independent implementation written
from the public documentation. A firmware image built by Plinky's IDE combines CLATTER 12 with
Plinky's firmware, whose licence is not published; see FEASIBILITY.md §5 before distributing such
an image.

## Source code

The complete corresponding source of CLATTER 12 is this repository with its submodule.
