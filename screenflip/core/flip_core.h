#ifndef FPSCREENFLIP_CORE_H
#define FPSCREENFLIP_CORE_H
#include <stdint.h>

/* Firmware-independent fpScreenFlip behaviour.
 *
 * One flip value per display custom: STILL and CINE each keep Custom 1..4
 * (solution 4 is "LCD off" and has no flip).  A value is applied when the
 * camera switches INTO that custom, never when it is edited.  The table is
 * saved after it has held still; the adapter owns the file and the clock.
 *
 * This file has no firmware address, no libc call and no writable global.  The
 * fp profile and the fp L profile supply the panel, the display-mode getters,
 * the recorder state, the private-variable queue and the store.  The reference
 * implementation and its measured fp values are in screenflip/native/flip.c.
 */

#define FPF_VALUES        4u
#define FPF_CUSTOMS       4u                  /* Custom 1..4; mode 4 = LCD off */
#define FPF_SETS          2u                  /* 0 STILL, 1 CINE */
#define FPF_SLOTS         (FPF_SETS * FPF_CUSTOMS)
#define FPF_NO_KEY        0xFFFFFFFFu
#define FPF_SAVE_POLLS    20u                 /* x the adapter's poll period */
#define FPF_SETTINGS_MAGIC 0x54534646u        /* "FFST" */
#define FPF_SETTINGS_WORDS 5u                 /* magic, version 1, 0..3, 4..7, check */

#define FPF_LAYER_MASK    0x00FFF000u

enum fpf_apply {
    FPF_APPLY_NONE = 0,                       /* the register is already right */
    FPF_APPLY_WRITTEN = 1,                    /* written and relaid out */
    FPF_APPLY_REFUSED = 2                     /* turns the UI, touch object unknown */
};

struct fpf_panel {
    uint32_t (*layer_read)(void *user);
    void (*layer_write)(void *user, uint32_t);
    uint32_t (*touch_read)(void *user);
    void (*touch_write)(void *user, uint32_t);
    uint32_t (*touch_supported)(void *user);
    void (*relayout)(void *user);
};

struct fpf_mode {
    uint32_t (*display)(void *user);           /* 0..4; 4 = LCD off */
    uint32_t (*cinema)(void *user);            /* 0 = STILL */
    uint32_t (*style)(void *user);             /* 1 = STILL set even in CINE */
    uint32_t (*editing)(void *user);           /* Custom 1..4 -> 0..3, else NO_KEY */
};

struct fpf_ops {
    struct fpf_panel panel;
    struct fpf_mode mode;
    uint32_t (*recording)(void *user);         /* nonzero: do not save */
    uint32_t (*store)(void *user, const uint8_t table[FPF_SLOTS]);  /* write+verify; 1 = on the card */
    void (*queue)(void *user, uint32_t value); /* show value in the menu row */
    void *user;                                /* the adapter's state; never a global */
};

struct fpf_state {
    uint8_t table[FPF_SLOTS];                 /* set * 4 + custom -> 0..3 */
    uint8_t saved[FPF_SLOTS];
    uint32_t live_key, edit_key;
    uint32_t last_apply;
    uint32_t save_pending;
    uint32_t polls, switches, applies, refused, pushes, edits, saves, save_failed;
};

void fpf_state_init(struct fpf_state *s);
uint32_t fpf_want(uint32_t value);
uint32_t fpf_touch(uint32_t value);
uint32_t fpf_apply(const struct fpf_panel *panel, void *user, uint32_t value);
uint32_t fpf_set_in_use(uint32_t cinema, uint32_t style);
uint32_t fpf_key(uint32_t set, uint32_t mode);
uint32_t fpf_editing_key(uint32_t editing, uint32_t set);
uint32_t fpf_changed(struct fpf_state *s, uint32_t key, uint32_t value);
void fpf_poll(struct fpf_state *s, const struct fpf_ops *ops);
typedef uint32_t (*fpf_apply_fn)(const struct fpf_panel *, void *, uint32_t);
void fpf_poll_using(struct fpf_state *s, const struct fpf_ops *ops, fpf_apply_fn apply);
uint32_t fpf_unsaved(const struct fpf_state *s);

uint32_t fpf_record_check(uint32_t lo, uint32_t hi);
uint32_t fpf_record_decode(const uint32_t record[FPF_SETTINGS_WORDS],
                           uint8_t table[FPF_SLOTS]);
void fpf_record_encode(const uint8_t table[FPF_SLOTS],
                       uint32_t record[FPF_SETTINGS_WORDS]);

#endif
