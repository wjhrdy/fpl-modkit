#include "flip_core.h"

/* No libc, no static writable data: the profiles include this file in the
 * same freestanding unit as their firmware bindings. */

static void count(uint32_t *n) {
    if (*n != 0xFFFFFFFFu) ++*n;
}

uint32_t fpf_want(uint32_t value) {
    return value == 1u ? 0x333000u : value == 2u ? 0x001000u : value == 3u ? 0x332000u : 0u;
}

uint32_t fpf_touch(uint32_t value) {
    return value == 1u || value == 3u;
}

uint32_t fpf_apply(const struct fpf_panel *panel, void *user, uint32_t value) {
    uint32_t want, now, touch_ok, touch;
    if (!panel || !panel->layer_read || !panel->layer_write ||
        !panel->touch_read || !panel->touch_write ||
        !panel->touch_supported || !panel->relayout)
        return FPF_APPLY_REFUSED;
    if (value >= FPF_VALUES) value = 0;
    want = fpf_want(value);
    touch = fpf_touch(value);
    now = panel->layer_read(user);
    touch_ok = panel->touch_supported(user);
    /* A turned UI with touch left upright would invert the touch screen. */
    if (touch && !touch_ok) return FPF_APPLY_REFUSED;
    if (touch_ok && panel->touch_read(user) != touch) panel->touch_write(user, touch);
    if ((now & FPF_LAYER_MASK) == want) return FPF_APPLY_NONE;
    panel->layer_write(user, (now & ~FPF_LAYER_MASK) | want);
    panel->relayout(user);
    return FPF_APPLY_WRITTEN;
}

uint32_t fpf_set_in_use(uint32_t cinema, uint32_t style) {
    return cinema == 0u || style == 1u ? 0u : 1u;
}

uint32_t fpf_key(uint32_t set, uint32_t mode) {
    return mode < FPF_CUSTOMS ? set * FPF_CUSTOMS + mode : 0x100u | set;
}

uint32_t fpf_editing_key(uint32_t editing, uint32_t set) {
    return editing < FPF_CUSTOMS ? set * FPF_CUSTOMS + editing : FPF_NO_KEY;
}

uint32_t fpf_changed(struct fpf_state *s, uint32_t key, uint32_t value) {
    if (!s || key >= FPF_SLOTS || value >= FPF_VALUES) return 0;
    if (s->table[key] != value) {
        s->table[key] = (uint8_t)value;
        s->save_pending = 0; /* count quiet polls from the last actual edit */
        count(&s->edits);
    }
    return 0;
}

uint32_t fpf_unsaved(const struct fpf_state *s) {
    for (uint32_t n = 0; n < FPF_SLOTS; ++n)
        if (s->table[n] != s->saved[n]) return 1;
    return 0;
}

void fpf_state_init(struct fpf_state *s) {
    if (!s) return;
    for (uint32_t n = 0; n < FPF_SLOTS; ++n) s->table[n] = s->saved[n] = 0;
    s->live_key = s->edit_key = FPF_NO_KEY;
    s->last_apply = FPF_APPLY_NONE;
    s->save_pending = 0;
    s->polls = s->switches = s->applies = s->refused = 0;
    s->pushes = s->edits = s->saves = s->save_failed = 0;
}

void fpf_poll_using(struct fpf_state *s, const struct fpf_ops *ops, fpf_apply_fn apply) {
    uint32_t set, mode, key;
    if (!s || !ops) return;
    count(&s->polls);
    if (!ops->mode.display || !ops->mode.cinema || !ops->mode.style || !ops->mode.editing)
        return;
    set = fpf_set_in_use(ops->mode.cinema(ops->user), ops->mode.style(ops->user));
    mode = ops->mode.display(ops->user);

    /* 1. switched into another display mode (or set): apply its value */
    key = fpf_key(set, mode);
    if (key != s->live_key) {
        s->live_key = key;
        count(&s->switches);
        if (mode < FPF_CUSTOMS) {             /* LCD off: nothing to flip */
            s->last_apply = apply(&ops->panel, ops->user, s->table[key]);
            count(s->last_apply == FPF_APPLY_REFUSED ? &s->refused : &s->applies);
        }
    }

    /* 2. the menu now edits another custom: show that custom's value */
    key = fpf_editing_key(ops->mode.editing(ops->user), set);
    if (key != s->edit_key) {
        s->edit_key = key;
        if (key != FPF_NO_KEY && ops->queue) {
            ops->queue(ops->user, s->table[key]);
            count(&s->pushes);
        }
    }

    /* 3. keep the store in step, once the table has held still and the camera
     * is not recording */
    if (!ops->store || !fpf_unsaved(s) ||
        (ops->recording && ops->recording(ops->user))) {
        s->save_pending = 0;
        return;
    }
    if (++s->save_pending < FPF_SAVE_POLLS) return;
    s->save_pending = 0;
    {
        uint8_t was[FPF_SLOTS];
        for (uint32_t n = 0; n < FPF_SLOTS; ++n) was[n] = s->table[n];
        if (ops->store(ops->user, was)) {
            for (uint32_t n = 0; n < FPF_SLOTS; ++n) s->saved[n] = was[n];
            count(&s->saves);
        } else {
            count(&s->save_failed);
        }
    }
}

void fpf_poll(struct fpf_state *s, const struct fpf_ops *ops) {
    fpf_poll_using(s, ops, fpf_apply);
}

static uint32_t pack(const uint8_t *t, uint32_t from) {
    return t[from] | t[from + 1] << 8 | t[from + 2] << 16 | (uint32_t)t[from + 3] << 24;
}

uint32_t fpf_record_check(uint32_t lo, uint32_t hi) {
    return FPF_SETTINGS_MAGIC ^ 1u ^ lo ^ hi ^ 0xFFFFFFFFu;
}

uint32_t fpf_record_decode(const uint32_t record[FPF_SETTINGS_WORDS],
                           uint8_t table[FPF_SLOTS]) {
    uint32_t ok;
    if (!record || !table) return 0;
    ok = record[0] == FPF_SETTINGS_MAGIC && record[1] == 1u &&
         record[4] == fpf_record_check(record[2], record[3]);
    for (uint32_t n = 0; ok && n < FPF_SLOTS; ++n)
        ok = ((n < 4u ? record[2] : record[3]) >> (8u * (n & 3u)) & 0xFFu) < FPF_VALUES;
    for (uint32_t n = 0; n < FPF_SLOTS; ++n)
        table[n] = ok ? (uint8_t)((n < 4u ? record[2] : record[3]) >> (8u * (n & 3u))) : 0u;
    return ok;
}

void fpf_record_encode(const uint8_t table[FPF_SLOTS],
                       uint32_t record[FPF_SETTINGS_WORDS]) {
    if (!table || !record) return;
    record[0] = FPF_SETTINGS_MAGIC;
    record[1] = 1u;
    record[2] = pack(table, 0);
    record[3] = pack(table, 4);
    record[4] = fpf_record_check(record[2], record[3]);
}
