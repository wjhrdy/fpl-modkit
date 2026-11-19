# Original port evidence (November 17, 2026)

Historical address comparisons; see the current guide for behavior.

## What is proven offline

| Item | Evidence |
|---|---|
| Official fp L update → `MAIN.bin` + `DATA.bin` | `fpSup/tools/fpl_extract.py`; both payloads match the inspected hashes |
| Display setting block addresses | instruction-shape audit against both official images; 40/40 instruction match for the seven getters/setter and 40/40 for relayout |
| Y2_5_1 page, donor, string pool and declaration | byte-identical to fp at `+0xFD40` |
| ScreenFlip FPUI row block | built by the unchanged fp builder against the identical stock page; 11,444 bytes, no file op and no hook op |
| Flip semantics | `fpSup/screenflip/core` plus 13 host tests (mode switch, editing, touch refusal, LCD off, save hold/recording, record corruption) |
| fp L profile wiring | `screenflip_fpl/native/profile.c` compiles freestanding for ARMv7; 7 host tests drive the core through fake MMIO/getters, including a refused touch turn and a blocked save while recording |
| fp reference product | the branch's `screenflip/build_v3_flip.py` called `make_sup(..., pad_to=...)`, which this v3 no longer accepts; removed, so the fp v3 suite runs again (13 tests pass) |
| First live read-only pass | 2026-11-17 on fp L 3.02: touch object `C3760828`, flag `C376082C`=0, vtable `C3760830`→`C0CCE65C`; editing object `C3762264`, word `C376228C`=0xFFFF closed and 0 for Custom 1; recording `C30338CC`=0; display layer `30190044`=0 beside live `30100004`/`30190004` |

The menu row is on **SYSTEM > Mode Settings (`Y2_5`) > Custom N > More options
(`Y2_5_1`) > TOOLS tab, fifth row**: `Screen Flip`, values Off / 180 / Mirror /
180+Mirror.

## fp L bindings

| Name | fp L 3.02 | fp 5.02 | Basis |
|---|---:|---:|---|
| NBU base / string pool | `C18D01A0` / `C18D01B4` | `C18C0460` / `C18C0474` | +0xFD40, bytes equal |
| Y2_5_1 entry offset | `0x127B649` | `0x127B649` | declaration bytes equal |
| Settings object | `C0055120` | `C0057AE8` | identical body |
| Display mode (0..4) | `C005F1E0` | `C0061BA8` | identical body |
| Cinema mode (STILL/CINE) | `C0055978` | `C0058340` | identical body |
| Shooting style | `C005F0A0` | `C0061A68` | identical body |
| Display mode setter | `C005F1A0` | `C0061B68` | identical body |
| LCD enabled | `C005F240` | `C0061C08` | identical body |
| Custom support | `C005F2E0` | `C0061CA8` | identical body |
| Relayout (message 4) | `C02E8468` | `C02E4420` | identical shape |
| System status object | `C00178D8` | `C00178D8` | identical bytes |
| Task create/start | `C0016A58` / `C0016BC0` | same | identical bytes |
| Sleep | `C03707F0` | `C03705D8` | identical shape |
| File open | `C03661C8` | `C0365FB0` | identical shape |
| GUI/registry/post/subscribe | `C055B208`, `C05D74E9`, `C05D75F9`, `C0590078`, `C0590128`, `C055D0C8` | fp analogues | `fpl_ui.py` live snapshot |
| Touch panel object | `C3760828` (flag `C376082C`, vtable slot `C3760830`) | `C3760E40` (flag `C3760E44`, slot `C3760E48`) | live read-only scan; vtable `C0CCE65C` matched |
| Touch panel vtable | `C0CCE65C` | `C0CBE37C` | `TouchPanelIcLPM032M366` RTTI in both images |
| Custom-editing object | `C3762264` (vtable slot `C376226C`→`C0CD62E8`, word `C376228C`) | `C376287C` (word `C37628A4`) | live scan; `-0x618` block shift; word reads `0xFFFF` |
| System status / recording | `C3033830` / `C30338CC` | `C3033834` / `C30338D0` | fp L getter returns `C3033830`; byte read 0 |

These are locators.  A matching shape is not permission to call a function
from an arbitrary task; the callers and locks still need review.
