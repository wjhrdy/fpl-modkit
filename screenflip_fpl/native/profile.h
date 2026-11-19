#ifndef FPLSCREENFLIP_PROFILE_H
#define FPLSCREENFLIP_PROFILE_H
#include <stdint.h>
#include "flip_core.h"

/* fp L 3.02 ScreenFlip profile: the core's panel/mode/store wiring.
 *
 * The four confirmed getters are called by absolute address only after their
 * first words match the inspected firmware.  The panel register, touch object,
 * custom-editing word and status/recording byte are live bindings: pass their
 * current-boot addresses in `struct fplflip_live`; zero means "not measured"
 * and init refuses.  The addresses in bindings.py are evidence, not portable
 * constants.
 *
 * The fp L adapter uses the shared per-custom poll and applies the selection
 * to picture data only: 0=Off, 1=180, 2=horizontal, 3=vertical.
 * The engine alternates two private buffers and publishes a complete frame
 * before changing the four picture fetch addresses. Both turned modes also
 * rotate the stock OSD pixels by 180°, independently of the picture. */

#define FPLFLIP_MODE_H          1u          /* engine.want: per-line swap */
#define FPLFLIP_MODE_180        2u          /* + line order reversed */
#define FPLFLIP_MODE_V          3u          /* line order reversed only */
#define FPLFLIP_FETCH_BASES     0x30190190u   /* four words: the ring quad */
#define FPLFLIP_UNPINNED        0xFFFFFFFFu   /* engine.flip: the display is
                                                 not reading our buffer yet */
#define FPLFLIP_RING_FIRST      0x49801000u  /* CPU view of ring buffer 0 */
#define FPLFLIP_RING_STRIDE     0x3FC000u
#define FPLFLIP_LINES           1080u
#define FPLFLIP_STRIDE          3240u
#define FPLFLIP_WORDS           (FPLFLIP_STRIDE / 4u)
#define FPLFLIP_FRAME_BYTES     (FPLFLIP_LINES * FPLFLIP_STRIDE)
#define FPLFLIP_ENGINE_BYTES    0x400000u    /* live-proven request size */

struct fplflip_live {
    uint32_t layer_register;      /* physical display layer register */
    uint32_t touch_flag;          /* touch 180 flag */
    uint32_t touch_vtable_at;     /* word holding the touch object's vtable */
    uint32_t touch_vtable;        /* the recognized vtable */
    uint32_t editing_custom;      /* 0..3 while Y2_5_1 edits, 0xFFFF otherwise */
    uint32_t status_object;       /* system status object, 0 = unknown */
    uint32_t recording_offset;    /* recording byte from status_object */
};

struct fplflip_engine {
    uint32_t want;                 /* the transform: 0 off, MODE_H/180/V */
    uint32_t on;                   /* buffers held and the display pinned */
    uintptr_t buf[2];              /* the private class-0 pair; BOTH must
                                     allocate: one buffer means writing the
                                     frame the display is reading (tearing),
                                     so the engine refuses to run */
    uint32_t flip;                 /* the buffer the display is pinned to */
    uint32_t desc[2][4];           /* allocator descriptors, zeroed when free */
    uint32_t bases[4];             /* the fetch bases as the firmware had them */
    uint32_t src;                  /* ring source cycling 0..3 */
    uint32_t fails;                /* allocator refusals (diagnostics only) */
    /* v19 instrumentation (the offsets are read live over USB):
     * passes counts completed transforms; last_ticks is the CCNT delta of
     * the last pass; cal is CCNT ticks per 100 ms (measured at init);
     * diag carries why the engine stopped: 1 = single-buffer refused.
     * v20 splits the pass: inv/xform/clean are the per-phase CCNT deltas
     * of the last pass (the host converts with the wall-clock pass rate). */
    uint32_t passes, last_ticks, use_stock_copy, diag;
    uint32_t inv_ticks, xform_ticks, clean_ticks;
};

struct fplflip {
    struct fpf_state state;
    struct fpf_ops ops;
    struct fplflip_live live;
    struct fplflip_engine engine;
    void *product;                /* the product's own name/path/state */
    uint32_t (*store)(void *product, const uint8_t table[FPF_SLOTS]);
    void (*queue)(void *product, uint32_t value);
    struct {
        uintptr_t buf;
        uint32_t desc[4], source[2], shown[2], bank[2], old_touch;
    } overlay;
};

enum fplflip_error {
    FPLFLIP_OK = 0,
    FPLFLIP_ARGUMENT = 1,
    FPLFLIP_FIRMWARE = 2,
    FPLFLIP_LIVE = 3
};

uint32_t fplflip_firmware_ok(void);
uint32_t fplflip_init(struct fplflip *p, const struct fplflip_live *live,
                      void *product,
                      uint32_t (*store)(void *product, const uint8_t table[FPF_SLOTS]),
                      void (*queue)(void *product, uint32_t value));
void fplflip_poll(struct fplflip *p);
uint32_t fplflip_engine_on(const struct fplflip *p);
#ifdef FPL_ROW_BENCH
void fplflip_row_bench(struct fplflip *p, uint32_t result[24]);
#endif

#endif
