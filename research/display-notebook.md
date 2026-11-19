# fp L display notebook

Historical sections retain earlier hypotheses. The November 19 recheck and later camera evidence supersede earlier claims about hardware Mirror and pinning.

## Result

The fp and fp L **display layer descriptor and register tables are
byte-identical**:

| Table | fp 5.02 | fp L 3.02 | Length | Bytes |
|---|---|---|---:|---|
| Layer descriptor table | `C07264C0` | `C0724000` | `0x100` | equal |
| Layer register table | `C07BF9C4` | `C07BD108` | `0x200` | equal |

The fp L table at `C072402C` contains the same `30190028` entries, and the fp L
table at `C07BD108` contains the same `30190070`, `301900D0`, … layer
addresses.  The display controller register map is therefore not what changed.

The fp L relayout function `C02E8468` is the exact counterpart of fp
`C02E4420`.  It does **not** write `0x30190044`; it queues **display message 4**
to the display manager.

## Relayout path

```
C02E8468(0)                         fp L relayout entry
  C02E9DC8()                        display manager singleton -> C342727C
  C02EA218(mgr, &msg)               allocate 0xC4-byte message
  [msg+4] = 4                       message type 4
  C02EA2E8(mgr, &msg)               queue send
  C02E9D78(&msg, 2)                 free
```

The manager task `FUN_c02ea7e0` switches on `[msg+4]`:

| Type | Handler |
|---:|---|
| 2 | `FUN_c02eac28` — layer descriptor apply |
| 3 | `FUN_c02eae90` — layer remove |
| **4** | **`FUN_c02eab58` — re-apply every active layer** |
| 5 | `FUN_c02eaba0` — re-apply selected layers |

`FUN_c02eab58` walks the six layer-object pointers at `manager+0x64` and calls
each layer object's `vtable+0xC` update method with the stored descriptor.
This is why a bare `0x30190044` write without relayout can leave the manager's
layer state and the hardware inconsistent — the observed glitch.

## Layer objects

Two output classes are present with RTTI and vtables:

- `XC_DisplayLcd` — RTTI `C0B8CAE4`, vtable `C0B8CAC0`.
- `XC_DisplayLcdJDI_LPM032M366C` — RTTI `C0B8CB80`, vtable `C0B8CB38`.

The LCD update path is `C030C680` → `C030C6F0`/`C030C768`, which call the
display HAL at `C0622148`/`C0623020` (geometry and colour-matrix work).  The
HAL slice also writes `30191200`, `301000xx` and `30002xxx`, not
`30190044`.  No fp L instruction references `30190044` directly; the fp
product's address was measured, not derived from code.

Ghidra slices and decompiled dumps (repository-local, ignored):
`fpSup/out/ghidra/` — `lcd.log`, `hal.log`, `relayout.log`, `dump*.log`.

## Interpretation

Two hypotheses remain, in order:

1. **Missing relayout** (leading): the register map is unchanged, the fp value
   is plausible, and the fp product always calls relayout after writing.  The
   shell trial wrote the register without it.
2. **Different global layer-control field**: `0x30190044` is not in the
   firmware's layer tables, so it may be a global control register whose
   address/bit layout is only established by live measurement.

**2026-11-17 15:03 follow-up:** the same write was repeated and a screen switch
(`MainB2` -> `LV_STILL_MEDIUM`) was used as the relayout trigger.  The register
held `0x00333000`, the screen glitched again, and restore returned it to
normal.  A screen switch is therefore **not** a substitute for the actual
`C02E8468(0)` message-4 relayout.  The live test still has to call the real
entry from the resident adapter; if that also glitches, stop using
`0x30190044` and locate the global control field in the layer descriptor.

## Webcam observation of the LCD (2026-11-17 15:24)

A webcam aimed at the rear LCD (shown in VLC) was used as the observer, because
the HDMI capture card does not show the LCD layers.

- Baseline: live view with the normal bottom UI.
- `0x30190044 = 0x00333000`, touch flag `1`: the LCD showed a **blank pink
  screen**, not a 180° rotation.
- Restore `0`/`0`: live view returned.

Images and hashes:
`~/.local/share/fpsup/observations/screenflip-lcd-20261117/`
(`lcd-baseline.png`, `lcd-flipped.png`, `lcd-restored.png`, `manifest.json`).

**Confirmed with a better observer (2026-11-17 16:01):** the Insta360 Link
webcam was viewed through QuickTime and the window captured directly
(`lcd-qt-*.png`).  Baseline and restored show the full live view; the written
state is a **blank light-blue LCD**.  This is not a subtle glitch or a missing
relayout: the fp `0x00333000` value is wrong for fp L.  The obvious next
isolation test is layer 3 alone (`0x001000` mirror, then `0x003000` 180), which
avoids the OSD layers 4/5 that the fp value also turns.

This is stronger than the earlier "glitch": the fp `0x00333000` value does not
produce an LCD flip on fp L; it blanks the LCD content.  The fp encoding is
therefore wrong for fp L, independent of the relayout question.  The relayout
call is still required by the product design, but it is not the whole cause.

The next test should use the HDMI-layer value `0x000333` only to validate the
nibble/layer numbering in the VLC feed, and the LCD layer map must be located
separately before any further LCD write.

### Layer-3 mirror isolation (2026-11-17 16:03)

`0x30190044 = 0x001000` (layer 3 horizontal only) produced a **vertically
striped/corrupted picture** while the UI text (`ISO AUTO`, `P 1/30`, `F-- ±0.0`)
stayed upright.  Restore returned live view.

This says the layer-3 bit is in the picture path, but a direct register write
without the display manager's geometry/stride update is not enough.  The next
meaningful test is the **actual `C02E8468(0)` call**, not another value.
Images: `lcd-layer3-mirror.png`, `lcd-layer3-mirror-restored.png`.

## Observer setup used

- Insta360 Link (UVC `2e1a:4c01`) aimed at the rear LCD.
- QuickTime Player → New Movie Recording → Insta360 Link, window visible.
- Capture the QuickTime window with `screencapture -l <window-id>` (get the id
  from `CGWindowListCopyWindowInfo`, not a screen rectangle).
- Pan/tilt/zoom via `uvc-util -V 0x2e1a:0x4c01` (`pan-tilt-abs`, `zoom-abs`);
  the original Link exposes standard UVC controls.
- VLC crashed on this device; do not use VLC for the LCD observer.

## ISP write path & ring geometry (2026-11-17, session 2)

### Ring buffer — format, orientation, access

- Physical quad ring `0x02600400 / 0x026FF400 / 0x027FE400 / 0x028FD400`,
  `0x3FC000` bytes each.  Readable from the shell via the **handle alias**
  `0x49801000 / 0x49BFD000 / 0x49FF9000 / 0x4A3F5000` (plain `mem_get` works on
  the handle addresses; the raw physical `0x2600400` maps elsewhere — do not
  use).
- Format: 1620×1080 YUYV, **tight 3240-byte stride (810 words/line)**.  Proven
  by adjacent-line correlation: +0.999 at 3240 spacing; 3264 spacing shows the
  12-px/line drift.  Image occupies lines 0..1079 (line 0 black), buffer
  headroom ≈1283 lines; tails are padding (white residue on b0/b1, black on
  b2/b3).
- **Orientation: top-down, right-side-up.**  Decoded buffer 0 vs a fresh
  webcam capture: profile corr normal `+0.336` vs flipped `-0.642`; 2D corr
  raw `+0.570` vs 180 `-0.346`.  There is no hidden bottom-up storage to
  exploit.
- The display layer-3 fetch dictates the layout: fetch bases
  `0x30190190-0x3019019C` = the four ring addresses (this is the register the
  "re-arm touch" rewrites), width reg `0x301901C8 = 0x10000654`
  (flags `0x1000` + 1620), words-per-line `0x301901A0 = 0x32A` (810),
  X window `0x301901C0 = 0x003203ff` (50..1023),
  Y window `0x301901C4 = 0x000602aa` (6..682).  The writer just matches the
  tight layout.

### The develop→ring channel engine (0x3022 bank)

- 16 instances `0x30220000..0x3022F000`; static config identical; the
  instance at `0x30221000` is the active one (status regs +0x28/34/38/3C show
  rare transients).
- Map: `+0x5C = 0x438` (1080), `+0x60 = 0x32A` (810 wpl), `+0xA4..+0xB0` = the
  four ring addresses (`+0xB4..+0xEC` = addr0 ×13), `+0x114 = 0x9F`,
  `+0x118 = 0xCC0`, `+0x120 = 0xAA`, `+0x124 = 0x400`,
  **`+0x15C/+0x160` = the "pair" `0x02BB9004 / 0x02C38806`**.
- Second bank `0x30230000..0x3023F000` handles the 1920-wide path (instance
  `0x30231000`: pair `0x02A400E0 / 0x02AFC872` at `+0x144/+0x14C`,
  `+0x194 = 0x4F8` (1272), `+0x198 = 0xCC0`, `+0x184 = 0x660` (1632)).
- Live cursors: `0x301803F8` (stays in buffer 0, parks at ~`+0xD5xxx`,
  advances `0xFF000` per ~33 ms cycle, wraps mod `0x3FC000`);
  `0x301A03F8` (rotates b2→b1 at ~33 ms).  Content-churn sampling proves the
  writer rotates b0→b1→b2→b3 at frame rate, ~2 slots ahead of the display
  read.
- RAM descriptor structures holding the quad (six sites): `0xC31D3A50`,
  `0xC31D3C54` (display family; after the addrs 24 × `0x30000000` mirror
  slots; `0x040002aa` = LCD 1024×682), `0xC31D8A18` (1920 family:
  (1272,1920)), `0xC31DD320` (ring channel: `{2, pair, (1080,1620)}`),
  `0xC31DDDA8`, `0xC31DF528`, `0xC31DF620`.  Region base `0xC31D0000` is
  registered in the region table at `0xC28B29xx` (type words 0x11/0x12/0x13).
- **No ARM code anywhere in MAIN.bin references the channel engine MMIO**
  (no ARM or Thumb literal pools, no movw/movt to `0x3018xxxx/0x301Axxxx/
  0x3022xxxx/0x3023xxxx`).  The engines are descriptor-driven from the ISP
  side; the ARM-visible artifacts are the RAM structures above.

### Display driver module (decompiled — out/ghidra/dispdrv.log, slice 0xC00E0000)

- Layer base table at **`0xC07BD108`**: `0x30190070 + 0x60·k` (k = 0..5); the
  code accesses it as `param*4 + -0x3f842ef8`.  Layer k's "+0x50" reg
  (`0x301900C0 + 0x60·k`; layer 3 = **`0x301901E0`**) = **per-layer software
  flip state, bits 0-1** (getter FUN_c00e0d58 reports flipped when
  `(reg & 3) != 0`).  FUN_c00e0a90(0) clears bit 0 of `0x301901E0` on the
  output-off path.
- Hardware flip reg `0x30190044` is read per-layer (bit 2k = h, bit 2k+1 = v)
  by FUN_c00e0df0 (the h-bit lands in param_2[10] with the layer shift).
- Display state object at `0xC31D403C-0xC31D406C` written by FUN_c00e0b20
  from an 11-word descriptor (field 3 must be < 0x1FE); the bools at
  `0xC31D403C / 0xC31D4040` gate enable-bit writes to
  `0x301903F8 / 0x301903C8 / 0x301903F0 / 0x301903C0`.
- Register table `0xC07BDA2C` lists an **unexplored window
  `0x30190B00-0x30190BB8`** plus `0x3019042C` programmed per-layer (offsets
  0/4/8/0xC).  Users of that table are outside the 0xC00E/0xC072 slices — the
  new `fpl_disp3` slice (`0xC07E0000`, out/ghidra/disp3.log) targets them.
- Other unexplored windows referenced by the driver: `0x30190800-0x301908F8`
  (coefficient-table bursts, gamma/CSC-like), `0x30193000-0x301930D4`
  (clock/reset-ish: `0x3019307c |= 1`, `0x30193024 = 3/0`,
  `0x301930d4 = state copy`).

### Where this leaves the 180°

- Display read side: the hardware v-bit is broken in the new fetch engine
  (exhaustive live proof, earlier sections).  The display driver library keeps
  per-layer h/v flip state and getters, but no alternate hardware path
  appeared in the decompile.
- ISP write side: the ring layout is dictated by the display fetch; the
  writer's channel engine exposes no identified direction field and resists
  poking (wedge incident above).  No ARM code drives the engines directly.
- Remaining live items (after power cycle + reconnect): read-only dumps of
  the unexplored windows (`0x30190Bxx`, `0x30193000`, `0x30190800`); one
  cheap test — set layer-3's +0x50 bits (`0x301901E0 = 0x10000003`) and cycle
  `SetDisplayMode` to see whether the firmware applies the software flip
  state to the hardware; mine the `fpl_disp3`/`fpl_ispch` slices for the
  `0x30190Bxx` semantics and the descriptor builder.
- If none of those yields a direction field, the honest conclusion is:
  **picture 180° on the fp L needs software frame rotation** — a per-frame
  native task rotating the newest completed ring buffer in place during the
  ~2-frame window before the display fetches it (~3.5 MB YUYV per frame,
  NEON-able well inside the window).

## ISP channel driver decoded — the write-path verdict (2026-11-17, session 2)

The module at **`0xC0140000-0xC01C0000`** (dense Thumb, 692 functions
decompiled in `out/ghidra/ispch.log`, project `fpl_ispch`) is the ISP channel
driver.  It programs every engine bank from **32-word descriptors** in its BSS
(`0xC31D8000+`, manager object at `0xC31D80B0`; banks seen in code:
`0x3020/0x3021/0x3022/0x3023/0x3018/0x301A`).

Key function **FUN_c01472f0(descriptor, dir, channel)** — the full
descriptor→engine map for the `0x3022` bank, per-channel offsets from the
rodata table at `0xC07D1AA4`:

- `+0x5C` (height) ← `word[1] bits[20:7] >> 7`
- `+0x60` (wpl, 810) ← `word[2] bits[13:0]`
- `+0x64` ← `word[0x1a] bits[9:0]`
- `+0xA4..+0xF8` (21 address slots) ← `word[3]` then `word[4..0x18] & 0x3fffffff`,
  each falling back to `word[3]` when zero — explains the "addr0 ×13" live
  pattern
- `+0x100` ← `word[0x19]`; `+0x80/+0x84` ← `word[0x1b]/word[0x1c]`
- control/geometry: `+0x44` ← `word[0] bit0 | word[0x1c] bit30>>2 |
  word[0x1a] bit11>>7`; `+0x6C/+0x68/+0x70/+0x58/+0x8C/+0x74/+0x7C/+0x4C/
  +0x88/+0x94` ← packed bitfields of `word[0]/[1]/[2]/[0x1a]/[0x1c]/[0x1d]`
- `dir != 0` reads the engine back into the descriptor (two-way sync).

The second config half (`+0x110..+0x16C`, another function) maps the crop
window: `+0x118` ← `word[2] & 0x3fff` (the **padded stride 0xCC0**),
`+0x114/+0x11C/+0x120/+0x128/+0x12C` ← packed size/crop fields, and
**`+0x15C/+0x160` ← `word[3] / word[4] & 0x3fffffff` — the "mystery pair" is
just window buffer addresses** (with the same fallback-to-`word[3]` pattern).
The `0x301A` engine's `0x02A400E0/0x02AFC872` pair is the same thing for its
descriptor.  Live values of both pairs fall inside ring buffers 1-3 — they are
window/phase addresses, not direction flags.

**Verdict on the write-side 180:** the ISP descriptors are pure
address+geometry+enable DMA configs.  There is no direction, flip, or
reverse-stride field in any of the decoded bitfields, and the ring layout is
dictated by the display fetch anyway.  "Write the image bottom-up" does not
exist as a firmware capability on the fp L.  The picture 180° therefore
requires **software frame rotation**:

1. Hook the develop frame-done event (fp L equivalent of the fp's early-Vd
   hook — the ISP module has the event machinery; its IRQ status regs appear
   at `0x301803E4` etc. in FUN_c0158a58 calls).
2. Rotate the newest completed ring buffer in place within the ~2-frame
   display lag window (NEON, ~3.5 MB YUYV: reverse line order + byte-reverse
   each 4-byte YUYV word), **or** rotate into a private buffer and pin the
   display's four fetch-base regs to it (zero race, ~2× bandwidth).
3. Mirror stays hardware: `0x30190044 = 0x001000` + fetch-base touch re-arm,
   so 180+Mirror = software rotation + `0x001000`.

## 2026-11-19: native 180+Mirror evidence recheck (fp L 3.02)

180° composed with horizontal Mirror is **vertical reflection only**. Pixel
order and YUYV pairs within a row stay unchanged. OSD rotation is independent.

- The archived `screenflip-lcd-20261117` PNGs match their manifest hashes,
  but **do not support the manifest's “clean” conclusions**. `flip-base.png`
  shows a recognizable desk scene. `flip-mirror-live.png` shows that scene
  reversed in coarse strips, with discontinuities. `flip-picture-vflip-pinned.png`
  and `flip-picture-180-pinned.png` show green horizontal lines under intact UI,
  not a recognizable flipped scene. A frozen or strongly correlated capture
  isn't evidence for correct scanout. Cycling a common pin has no proven
  clean single-frame baseline.
- Independent Thumb disassembly of MAIN
  (`fb8c20968e01ff9d8edf5f3e3f681c7341958048a004e717092c75e11106d4b9`):
  `C00E0E54..C00E0E70` shifts `30190044` by **4 × layer**, then returns
  the low bit in descriptor `+0x28`; it doesn't return a second orientation
  flag there. `C00E0D58` reads the separate layer `+0x50` low two bits
  without establishing their meaning.
- Independent fp L evidence for an upstream hardware candidate: the diagnostic
  at `C0158688` references `"ROTATE\n"` (`C07D0DE8`), reports `301AD000`,
  `301AD010`, `301AD040`, then selects `"BUFEF1\n"` at `C01586D0`.
  Setup code at `C0136B2E` and `C0137634` constructs descriptors naming
  `301AD000`; the former passes its descriptor to `C019B078`. These correspond
  to the fp reference sites `C013A2AE` and `C013ADB4`. Neither the diagnostic
  nor that setup proves vertical reflection, live-view YUYV compatibility,
  or availability while recording. The discriminating test is an asymmetric
  off-screen frame with distinct rows and left/right markers: require row
  `1079-y` at output `y`, unchanged within each row. Establish supported
  modes and format from stock callers before testing throughput or live routing.

### ROTATE parameter experiment (fp L 3.02; offline CPU replay)

`C01AA0D8` reaches `C0136320` via `C01AA1FA` for the 1620×1080
dimension range. Four floats are forwarded. Replayed the actual Thumb/VFP
instructions `C0136440..C01365B4` in Unicorn with `d9=0.5`, `d10=16`
as set by the prologue, and read the four converted stack words at `sp+4`.
With all other inputs zero:

| Input varied | Positive | Negative |
|---|---:|---:|
| Float 0, magnitude 1 | `8000` | `8000` |
| Float 1, magnitude 1 | `8000` | `8000` |
| Float 1, magnitude 0.5 | `4000` | `C000` |

The first pair is scaled by 32768, rounded, and narrowed to 16 bits.
`C0136770..C0136786` stores them in the descriptor at `+8/+A`.
The last pair uses a scale of 16; `1079` in float 3 becomes `4370`.
These are encoding results, **not** transformed pixel coordinates: there is
no silicon model in the replay. Flipping the sign of a unit coefficient
is not a distinct command through this encoding. The logical axes, packing
and input/output format remain unresolved.

Subsequent stock caller trace resolves the coefficient question. At
`C042DD44..C042DDDC` the ARM caller applies rows `[a,-b,-tx]` and
`[b,a,-ty]` to image coordinates. `C042D21C..C042D278` packs the floats as
`[b,a,tx,ty]` before the `BLX C01AA0D8` at `C042D2BC`. The determinant is
`a*a+b*b`: rotation and uniform scale preserve handedness, and translation
cannot turn either into a vertical reflection. The alternate builder at
`C0136C20` likewise stores only two coefficient halfwords at `+8/+A`.
This excludes reflection via the traced coefficients, not an independent
hardware mode or a reversed LCD fetch origin.

Separately matched the fp L stock `copyRect` wrapper (`C0021548`), validator
(`C00F8210`) and converter (`C00F8168`). It passes separate image widths,
multiplied by bytes per pixel, as the two pitches. The validator compares
the widths against format-specific maxima as unsigned values. Negative
destination pitch is not a supported shortcut through that wrapper. This
does not characterize undocumented DMA modes.

### Last-visible-row LCD origin: live negative result

fp L 3.02, non-recording. The picture pitch read back as 810 words.
With all four fetch registers set to the second stock buffer (`026FF400`)
and orientation zero, QuickTime showed the intact desk scene. With all four
set to `027D4A06` (`026FF400 + 1079 × 810`, word addresses) and orientation
`00002000`, register readback matched the request but both captures showed
purple noise with intact upright OSD. Restored and read back orientation
zero and the original four distinct bases; the scene returned. This tests
a common last-row origin on an existing stock buffer, not a private test
pattern or four independently advancing last-row origins.

### Two additional stock display-driver paths (offline, fp L 3.02)

- `C00E49D8` accepts 0..3, swaps encodings 1 and 2, and updates **only
  bits 25:24 of `30190044`**. This is separate from the six per-layer
  nibbles. The setup at `C00DCC0C..C00DCC16` calls the per-layer setter
  and then this setter, with the latter's argument from `C00DAFE0` (zero
  or a mode-dependent RAM byte). No demonstrated rear-LCD behavior for
  this field. In particular it is not proven to flip just the picture.
- `C00E5198` tests both the layer mode (`C00E5030`: for layer 3,
  `301902C0[19:16]`) and decoded orientation (`C00E5CA0`). For mode 10
  and V or H+V, it **subtracts** the word pointed to by the per-layer table
  entry (`C2F0B4D4` for layer 3) from the supplied address. Otherwise it
  returns the address unchanged. `C00E51D8` applies this to all four inputs
  for picture layers before the writes. Under the same mode/orientation
  condition, `C00DB6D2` calls `C00E5150` to adjust two other table-selected
  words if bit 10 is set. No evidence yet that the current LCD mode is 10
  or that the subtracted quantity is a row. This is not the manual
  `+1079*810` trial. Do not force format/mode 10 merely to enter that branch.

Live follow-up on the rear LCD (still live view, not recording): wrote bits
25:24 of `30190044` as 1, 2, 3 in turn with the picture/OSD flip bits zero
and the stock quad untouched. All three values held on readback before and
after paired QuickTime captures. Red object stayed at the top, status text
upright in each. Restored and verified `30190044=0` and the original quad.
`301902C0=072C0222`: layer 3 mode is **12**, not 10. The pointer at
`C2F0B4D4` resolved to `301901A0`, value 810 (one row in fetch-address
units). Thus the firmware's one-row subtraction is real but inapplicable
in this live-view format; no format change or address correction was made.

### On-camera vertical copy benchmark (2026-11-19)

`build_v3_flip_fpl.py --row-bench` selects a diagnostic build. RBW1 card
SHA-256 `a49a3f23b494bb64da3136ee24a190d0a278563fcbcf7c9eb0a36be8989ab1d6`.
Two private full frames; source word is word index XOR a pass-specific
constant. Same 64-row cache preparation, grouped copy, and publish as the
live vertical mode. Five passes, all 874,800 destination words checked per
pass, **zero mismatches**. Stock timer reported 100,978 µs for a 100 ms
task sleep. Results in µs:

| pass | prepare | reverse-row copy | publish | total |
|---:|---:|---:|---:|---:|
| 1 | 690 | 188406 | 51 | 189183 |
| 2 | 685 | 188965 | 51 | 189741 |
| 3 | 679 | 187084 | 51 | 187846 |
| 4 | 685 | 187172 | 51 | 187939 |
| 5 | 682 | 188559 | 52 | 189325 |

This measures the private-frame copy, not LCD frame rate. The engine was
later observed on with `want=3` and had completed 87 real passes when Off
was next sampled. An Off screenshot showed horizontally displaced OSD icons
even with register zero and stock fetch bases; it does not establish why.

RBW2 card `06747da3fc8f3f0bea95b10f3c506cd2de52d41786b3458b65d8196bc4052ed6`
checked both mapped views before use. Full-frame times (µs, including cache
preparation and publication): original 186329; cached source only 90033;
cached destination only 189672; both 92402 and 92427. All five verified
against their distinct row patterns with zero word mismatches, including
reads through the original destination view. Source caching was the win;
the regular build keeps writes in the allocator's original view.

With RBW2's menu-selected V active (`want=3,on=1`), 12 passes completed in
1.96 s (~6.1 passes/s); this is not a measurement of unique LCD frames.
`330000` OSD rotation showed menu residue and noise in the LCD picture.
From Off after a user half-press restored the stock overlay, setting only the
software V request (orientation register 0 throughout) produced 16 passes
across five captures, with the red object down, upright complete status layer
and no noise band visible in the first/last captures. Original quad and Off
were verified afterwards. Regular picture-only card:
`45676fb7e57747eb637cb1e0747e7b5ed1d9ae6d054e15d13dda441a19b6055f`.

### OSD orientation for a physically inverted fp L

Requirement: rotate UI glyphs and positions 180° in both 180 modes;
Mirror affects only the picture. Live 3.02 layer isolation: `0x030000`
alone exposed stale menu text and noise, `0x300000` alone produced a grid
instead of status text. Half-press under `0x330000` did not fix it. Everything
returned to normal with the register restored. No BF behavior inferred.

In normal live view, independent reads of the layer buffers located a blank
1024×682 indexed surface at layer 4 (256 words/line) and the actual icons and
focus box in the alpha channel of layer 5 (512 words/line, 16 bits/pixel).
Build `0ee697e305058e379f46c72dd359af784a4ab3d4ea45f05a014b60b55ed1fb67`
turns both stock surfaces into private buffers in software, with no hardware
flip bits. Keeps indices and the component nibbles of each pixel intact.
Verified after boot (`READY`, advancing polls): selected the running
engine modes directly for the LCD trial. Both 180 and 180+Mirror showed
the same upside-down, non-reflected glyphs; the picture differed in left/right
orientation. Plain Mirror showed the right-side-up OSD. A half-press in
180+Mirror changed ISO AUTO to ISO 250 with the overlay still turned.
No stale menu or grid in these captures. Released S1 and restored Off.

### 180+Mirror stock row-copy A/B (fp L 3.02)

RBW3 card `c957b16b54bb8689bd6d7843d5fd38cf1156cc0d91d3fe9d4dbaca19d4567f1f`.
Stock ARM copy at `C001510C`, cached source and original destination, 1080
calls of 3240 bytes. Same-boot complete off-screen pass times in order (ms):
local 204.534, stock 25.958, local 203.524, stock 25.791, stock 27.288.
All 4,374,000 destination words verified. The older RBW2 local result was
90.033 ms; do not generalize the RBW3 local timing to the previous build.
Live engine counter in mode V: previously installed overlay build 6.22/s;
RBW3 using stock copy 131 passes in 5.48 s (23.90/s). OSD remained rotated.
These count completed transforms, not distinct LCD frames or tear-free
refreshes. No changes to source selection, panel handoff, or frame pacing.
