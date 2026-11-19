# fp L ModKit

An independent fpSup-derived development tree for **SIGMA fp L firmware 3.02**. The first mod is **Screen Flip**, including the per-card preferences tested on the physical camera. This is not a release of the original fpSup project.

**Screen Flip** adds a row under **SYSTEM → Mode Settings → Custom → More options → TOOLS**. Off, 180°, Mirror, and 180° + Mirror are saved independently for the four STILL and four CINE customs. Both 180° modes rotate the status overlay so you can read it with the camera upside down. See the [user guide](screenflip_fpl/README.md) and [camera results](research/README.md).

## Build and test

Use Python 3, Clang with ARMv7 support, and your own extracted fp L 3.02 `MAIN.bin`. No original fp image is needed. From this directory:

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python -B -m unittest discover -s screenflip/core
.venv/bin/python -B -m unittest discover -s screenflip_fpl/native
FPSUP_FPL_MAIN=/path/to/FPL_V302/MAIN.bin .venv/bin/python -B -m unittest discover -s screenflip_fpl
.venv/bin/python -B screenflip_fpl/build_v3_flip_fpl.py \
  --fpl /path/to/FPL_V302/MAIN.bin --out build/new-screenflip
```

Use a fresh output directory for each build. `4xFLIP.BIN` is a **boot-bin payload**, not a file to rename to `fpSup.BIN` at the card root. The camera-tested card used this payload at entry offset zero inside a compatible ordinary fp L card package. Card packaging is a separate step; a bootloader, AutoRun, or USB shell is not required to build or test the mod here.

## Start another mod

| Directory | Use |
|---|---|
| `screenflip_fpl/` | A complete example: build script, declarative menu row, native application, camera profile, test fixtures. |
| `screenflip/core/` | Firmware-independent per-display settings, record validation, debounce and mode transitions. |
| `uishare/` | The shared native menu implementation selected for fp L 3.02, plus the offline FPUI format. |
| `tools/` | Host ARM object assembly and validation. |
| `research/` | Display/OSD notebook, firmware bindings, screenshots and timing data. |

Put a new product in its own directory alongside `screenflip_fpl/`. Keep its value semantics and persistence format testable on the host. Use a distinct UI variable and preference filename. Verify addresses and resource hashes against the fp L image; keep screenshots, timings and tested payload hashes with the product. Do not use a display preference to write camera-internal settings.

**Provenance:** derived from [ijigen/fpSup](https://github.com/ijigen/fpSup) through the [aquarat fp L port](https://github.com/aquarat/fpsup/tree/fp-l-302-port), with the working-tree Screen Flip mod and research added. Upstream attribution and terms are in [NOTICE](NOTICE) and [LICENSE](LICENSE). No Sigma firmware is distributed.
