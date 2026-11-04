# Third-party components and references

| Component | Use | Licence | Where |
|---|---|---|---|
| CLATTER Pocket, commit `f3ec3ac4ebc92bc75ba8e625ddf411c658eb4777` | engine host, sequencer, generator, kit designer, single-precision config, packed tables | GPL-3.0-only (+ LXR section 7 terms) | `third_party/clatter-pocket` (submodule, unmodified) |
| Erica Synths LXR-02 firmware sound engine (via CLATTER and Pocket) | the synthesis | GPL-3.0-only + section 7 terms | inside the submodule, `lxr/upstream` |
| CLATTER's LXR shim and parameter handler (via Pocket) | platform shim, parameter dispatch | GPL-3.0-only | inside the submodule, `lxr/shim`, `lxr/bridge` |
| Plinky 12 panel API documentation and header bundle (plinky12.com) | interface reference; downloaded at build time for `tools/check_api.py` and macro names (`tools/plinky_macros.txt` lists names only) | not stated; not redistributed here | `build/plinky_library.h` (not committed) |
| Plinky 12 IDE | compiles and flashes the panel | Plinky's terms | plinky12.com |

References consulted, not included: the Plinky 12 community panels
([plinkysynth/community-panels](https://github.com/plinkysynth/community-panels)) and
[charlesvestal/plinky-ambiotica](https://github.com/charlesvestal/plinky-ambiotica) (MIT), whose
porting notes report the 32 kHz / 2000 µs budget, the source-size limit and PSRAM behaviour. No code
from either is used.

The host tools use only the C library and Python's standard library. Pillow was used once to make
`docs/images/*.png` from the simulator's PPM frames.

## Build tools for the web module (not included)

`docs/assets/c12.wasm` is built from this repository's sources (the engine text, `core/c12_wasm.c`, and CLATTER's libm
shim from CLATTER Pocket) with LLVM's clang and `wasm-ld`. No third-party code from those tools is in the module beyond
compiler-generated code. The module is a derived work of the LXR-02 sound engine and is GPL-3.0-only with the same
additional terms (NOTICE.md); its complete source is this repository.
