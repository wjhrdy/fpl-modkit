#ifndef FPLSCREENFLIP_PANEL_H
#define FPLSCREENFLIP_PANEL_H
#include <stdint.h>

/* fp L 3.02 display-flip panel semantics (2026-11-17, live-verified).
 *
 * The display controller global register 0x30190044 has six layer nibbles,
 * but only bits 0 and 1 of each nibble are writable; bits 2-3 read back 0.
 *
 *   nibble 3 (bits 12-15) = LCD picture layer
 *     bit 0 (0x001000) = horizontal mirror   WORKS live
 *     bit 1 (0x002000) = vertical            CORRUPTS the rotating liveview
 *                                            fetch (hardware limitation)
 *   nibble 4 (bits 16-19) = LCD OSD main
 *     bit 0 (0x010000) = horizontal mirror   WORKS live
 *     bit 1 (0x020000) = vertical flip        WORKS live
 *   nibble 5 (bits 20-23) = LCD OSD sub (same layout)
 *   nibble 0 (bits 0-3)   = HDMI picture (drives HDMI, not the LCD)
 *
 * A flip bit takes effect cleanly only after the layer's fetch is re-armed:
 * writing any register of that layer's 0x30190070+0x60*n block (even the same
 * value) restarts the scanout.  The relayout entry C02E8468(0) also re-arms,
 * but touching the layer register directly is sufficient and cheaper.
 *
 * Verified live states (Insta360 Link webcam -> QuickTime observer):
 *   0x001000 + re-arm  : picture mirrored, UI normal           (fp Mirror)
 *   0x111000 + re-arm  : picture and UI all mirrored
 *   0x020000 + re-arm  : UI upside down
 *   0x002000 (any)     : picture sheared/corrupt
 *   0x333000 + pinned  : UI inverted live, picture inverted but FROZEN
 */

#define FPLFLIP_LAYER_REG     0x30190044u
#define FPLFLIP_PICTURE_H     0x001000u   /* nibble3 bit0: live mirror */
#define FPLFLIP_PICTURE_V     0x002000u   /* nibble3 bit1: corrupts live fetch */
#define FPLFLIP_OSD_H         0x010000u   /* nibble4 bit0: live UI mirror */
#define FPLFLIP_OSD_V         0x020000u   /* nibble4 bit1: live UI v-flip */
#define FPLFLIP_OSD2_H        0x100000u   /* nibble5 bit0 */
#define FPLFLIP_OSD2_V        0x200000u   /* nibble5 bit1 */

/* Layer register blocks (base from the firmware table at 0xC07BD108). */
#define FPLFLIP_PICTURE_BLOCK 0x30190190u  /* nibble-3 layer: 4 buffer addrs +0x00 */
#define FPLFLIP_OSD_BLOCK     0x301901F0u  /* nibble-4 layer */

/* The product's ScreenFlip values map onto these bits:
 *   0 Off    = 0
 *   2 Mirror = FPLFLIP_PICTURE_H (0x001000) + re-arm   -- same value as fp
 *   1 180    = needs PICTURE_V; live 180 is not achievable via this register
 *   3 180+M  = same limitation
 */

#endif
