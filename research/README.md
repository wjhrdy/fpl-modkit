# Screen Flip: what was verified

Use the [display notebook](display-notebook.md) for detailed experiments and the [port bindings](port-bindings.md) for their origins. The earliest notes predate the on-camera corrections. Named files and paths in historical excerpts refer to the original research workspace; portable artifacts included here are listed in the [evidence manifest](evidence/manifest.json).

| Question | Result and evidence |
|---|---|
| Can the LCD picture registers alone do the job? | Hardware Mirror produced strips; pinning a vertical fetch to the last row produced noise. The [stock picture](evidence/flip-base.png), [hardware Mirror](evidence/flip-mirror-live.png), [last-row capture](evidence/last-row.png), and [upper-field comparison](evidence/upper-field.jpg) document the negative results. |
| Can the overlay be read upside down? | The [hardware OSD experiment](evidence/osd-hardware.jpg) showed stale text/grid artifacts. The software overlay is readable with the camera inverted in [180°](evidence/overlay-180.png) and [180° + Mirror](evidence/overlay-180-mirror.png). It updates on [half-press](evidence/overlay-half-press.png). |
| How fast was the vertical flip? | [Stock row-copy A/B](evidence/row-bench-3.json): 25.8–27.3 ms per private frame, zero mismatched words. Live [baseline](evidence/baseline-rate.json) 6.2 vs [stock copy](evidence/stock-live-rate.json) 23.9 **completed transforms/s**. This does not measure unique LCD refreshes; some tearing was observed. |
| Does the setting survive reboot? | A 20-byte FFST record encoded STILL Custom 1 = Off and STILL Custom 2 = 180° + Mirror. After reboot Custom 2 was active with a matching saved table and zero new saves; changing customs gave [Off](evidence/prefs-custom1.png), then [the preferred flip](evidence/prefs-custom2.png). |

## Reproduction identities

- External fp L 3.02 MAIN: 49,310,208 bytes, SHA-256 `fb8c20968e01ff9d8edf5f3e3f681c7341958048a004e717092c75e11106d4b9`.
- Persistence payload: 28,672 bytes, SHA-256 `c3d72f8b26b9825a06b8eb1d6d97d5c6328138b99522a271f058b7ea98d4487f`. The fork-only build was compared byte for byte to the boot-bin used in the camera test.
- Camera-tested merged card: 33,848 bytes, SHA-256 `a515997c06b23766f911776c51a1e1284dfa73a0b63da7f829525fdb7a5f621c`.
