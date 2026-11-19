#ifndef FPLSCREENFLIP_FLIP_H
#define FPLSCREENFLIP_FLIP_H
#include <stdint.h>
#include "profile.h"

/* fpScreenFlip for fp L 3.02, as a Loader v3 sup (\fpSup\4xFLIP.BIN), the
 * mirror of screenflip/native/flip.h on the fp.  Every firmware address is
 * the fp L's own: the UI services were image-reviewed in fpl_ui.py and the
 * whole set was live-fingerprinted on 2026-11-18 (first words in
 * fp_usb_shell/screenflip_fpl_checks_live.json); GUI_OBJECT 0xC37B6B10 was
 * proven by calling GUI_GET 0xC055B208 through the pool-staging echo.
 *
 * The embedded FFST record supplies defaults. Per-card overrides live in
 * \FPFLIP.CFG; no offset in the merged BIN is used for a write. */

#define FPL_LAYER_REG    0x30190044u
#define FPL_TOUCH_FLAG   0xC376082Cu
#define FPL_GUI_OBJECT   0xC37B6B10u    /* [word] = the UI app this boot */
#define FPL_APP_REGISTRY  0x888u
#define FPL_REGISTRY_OFF  0x60u
#define FPL_LOOKUP       0xC05D75F8u   /* registry lookup, Thumb */
#define FPL_REGISTER_FN  0xC05D74E8u   /* registry register, Thumb */
#define FPL_SUBSCRIBE_FN 0xC055D0C8u   /* subscribe (g, name, fn): REJECTED on
                                         fp L -- its callback-pool + registry
                                         work ran only in the firmware's own
                                         setup contexts; called from this
                                         payload's context it wedged the whole
                                         camera (2026-11-18, gated stepper:
                                         gates 1-8 all passed, death at 9).
                                         The row's value word is polled from
                                         the task instead (fpl_task). */
#define FPL_POST_INT     0xC0590078u   /* queue a UI variable write */
#define FPL_TASK_CREATE  0xC0016A58u
#define FPL_TASK_START   0xC0016BC0u
#define FPL_SLEEP        0xC03707F0u
#define FPL_PUBLISH      0xC000E91Cu   /* D-cache clean+invalidate */
#define FPL_POLL_MS      25u           /* the rotation pass runs inside this */
#define FPL_TASK_PRIORITY 20u
#define FPL_TASK_STACK   0x2000u       /* the task installs the row itself (the
                                         inlined uia_apply frame is ~2.3 KB),
                                         unlike the fp's shallow poll task */
#define FPL_VARIABLE_INT 0u

#define FPL_TOUCH_VTABLE_AT  0xC3760830u
#define FPL_TOUCH_VTABLE     0xC0CCE65Cu
#define FPL_EDITING_CUSTOM   0xC376228Cu
#define FPL_STATUS_OBJECT    0xC3033830u
#define FPL_RECORDING_OFF    (0x18u + 0x84u)

enum fpl_result {
    FPL_READY = 1,
    FPL_NO_GUI = 2,
    FPL_REGISTER = 3,
    FPL_VARIABLE = 4,
    FPL_FIRMWARE = 5,
    FPL_SUBSCRIBE = 6,
    FPL_UI = 7,
    FPL_TASK = 8,
    FPL_INVALID = 9,
    FPL_PROFILE = 10,
    FPL_DEFERRED = 11            /* entry done, the row installs from the task */
};

struct fpl_card {
    /* entry_fpl.S fills these four before fpl_card_init */
    uintptr_t ui_at;                 /* the row's FPUI block */
    uint32_t ui_len;
    uintptr_t task_entry;           /* task_shim: fpl_task(card) */
    uintptr_t changed_entry;        /* changed_shim: fpl_changed(descriptor, card) */
    uint32_t magic;                 /* "FLIP" */
    uint32_t result;                /* enum fpl_result, 0 = never tried */
    uintptr_t base;                 /* the blob (entry_fpl.S `entry`) */
    const char *self_path;          /* unused on the merged boot-bin path */
    uint32_t name[5];               /* "MV_fpScreenFlip": the registry and the
                                       GUI queue BORROW it */
    uint32_t registered, subscribe_result, ui_result, ui_op, first_id, row_y;
    uintptr_t page;                 /* the Y2_5_1 copy switched in */
    int32_t task_id;
    struct fplflip flip;            /* the profile: state, ops, engine */
    /* the deferred install (v2): the UI is not touched from the loader's
     * task; the row goes in from OUR task once the app word is valid */
    uint32_t install;               /* 0 pending, then a fpl_result code
                                        (FPL_EXPERIMENT_B: the stepper's word) */
    uint32_t install_polls;          /* task polls since load (diagnostics) */
    uint32_t app_word;               /* the last GUI app word seen */
    volatile uint32_t gate;          /* FPL_EXPERIMENT_B: the host releases each
                                        step by writing this over the shell */
    uintptr_t desc_word;             /* v12: the variable's descriptor; the task
                                        POLLS its value word instead of a
                                        subscribed callback */
    uint32_t last_value;             /* the value word as the task last saw it */
    uintptr_t exp_ops[4];            /* FPL_EXPERIMENT_B (v13): the saved real
                                         mode ops, restored one gate at a time */
    _Alignas(8) uint32_t prefs_file[1024]; /* task-owned FW file object */
#ifdef FPL_ROW_BENCH
    uint32_t bench[24]; /* state tail: signature, samples, errors, phase times */
#endif
};

uint32_t fpl_changed(uintptr_t descriptor, struct fpl_card *c);
void fpl_task(struct fpl_card *c);
uint32_t fpl_card_init(struct fpl_card *c, uintptr_t base, const char *path);
#endif
