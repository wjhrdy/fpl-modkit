#include "flip_fpl.h"
#include "ui_apply.h"     /* fpSup/uishare: pages composed from each sup's FPUI block */

/* fp L 3.02 facts.  UI registry (fp_usb_shell/fpl_ui.py, live 2026-11-18):
 *   [C37B6B10] = the UI app (GUI_GET 0xC055B208 returns exactly this word);
 *   app+888 -> the variable registry object, registry+0x60 its base,
 *   registry+0x14 the embedded name map (count 0x896 this boot)
 *   C05D75F8  lookup(registry, name, &descriptor), Thumb: 0 for hit and miss
 *   C05D74E8  register(app, count, {type, name, value}...), Thumb
 *   C055D0C8  subscribe(GUI object, name, fn): fn(descriptor, x) on the UI
 *             thread, the new value at descriptor +8
 *   C0590078  queue a UI variable write (0, name, value): the GUI thread
 *             does it later, so the name must outlive the call
 * Tasks: tk_cre_tsk 0xC0016A58, tk_sta_tsk 0xC0016BC0, sleep 0xC03707F0.
 * Cache: 0xC000E91C clean+invalidate, 0xC000EABC I-cache.
 * The panel/touch/editing/recording bindings are passed to the profile as
 * live values (struct fplflip_live); the engine's allocator and ring views
 * are in profile.c with their own checks.  Each called routine's first word
 * is checked before the first call (screenflip_fpl_checks_live.json). */

#define GUI_OBJECT     FPL_GUI_OBJECT
#define APP_REGISTRY   FPL_APP_REGISTRY
#define REGISTRY_OFF   FPL_REGISTRY_OFF
#define VARIABLE_INT   FPL_VARIABLE_INT
#define DESC_TYPE      0x00u
#define DESC_NAME      0x04u
#define DESC_VALUE     0x08u
#define CARD_MAGIC     0x50494C46u      /* "FLIP" */
#define USED __attribute__((used))

#if !defined(__arm__) || UINTPTR_MAX != UINT32_MAX
#error "ARM32 native ABI required"
#endif
#ifndef FPF_SETTINGS_OFF
#error "FPF_SETTINGS_OFF: entry_fpl.S `settings`, from the blob's start (builder)"
#endif

#define peek(a) (*(volatile const uint32_t *)(a))
#define poke(a, v) (*(volatile uint32_t *)(a) = (v))

#define CHECKS(X) \
    X(0xC05D75F8u, 0x3014B510u)  /* lookup (Thumb) */ \
    X(0xC05D74E8u, 0x4FF0E92Du)  /* register (Thumb) */ \
    X(0xC055D0C8u, 0xE92D49F0u)  /* subscribe */ \
    X(0xC0590078u, 0xE92D4070u)  /* queue a variable write */ \
    X(0xC0016A58u, 0xE92D0010u)  /* tk_cre_tsk */ \
    X(0xC0016BC0u, 0xE92D0010u)  /* tk_sta_tsk */ \
    X(0xC03707F0u, 0xE92D4030u)  /* sleep */ \
    X(0xC04408B8u, 0xE92D4010u)  /* filesystem manager */ \
    X(0xC04408F8u, 0xE92D44F0u)  /* volume */ \
    X(0xC03660A8u, 0xE92D4030u)  /* file ctor */ \
    X(0xC03661C8u, 0xE92D4070u)  /* open */ \
    X(0xC0366278u, 0xE92D44F0u)  /* read */ \
    X(0xC0366300u, 0xE92D40F0u)  /* write */ \
    X(0xC0366238u, 0xE92D4010u)  /* close */ \
    X(0xC03660E8u, 0xE92D4030u)  /* dtor */

static uint32_t firmware_ok(void) {
#define CHECK(at, word) if (peek(at) != (word)) return 0;
    CHECKS(CHECK)
#undef CHECK
    return fplflip_firmware_ok();
}

static uint32_t lookup(uintptr_t r, const char *n, uintptr_t *d) {
    return ((uint32_t (*)(uintptr_t, const char *, uintptr_t *))(FPL_LOOKUP | 1u))(r, n, d);
}
static uint32_t reg(uintptr_t a, uint32_t c, const uint32_t *d) {
    return ((uint32_t (*)(uintptr_t, uint32_t, const uint32_t *))(FPL_REGISTER_FN | 1u))(a, c, d);
}
static uint32_t subscribe(uintptr_t g, const char *n, uintptr_t f) {
    return ((uint32_t (*)(uintptr_t, const char *, uintptr_t))FPL_SUBSCRIBE_FN)(g, n, f);
}
static void post_int(uint32_t x, const char *n, uint32_t v) {
    ((void (*)(uint32_t, const char *, uint32_t))FPL_POST_INT)(x, n, v);
}
static void publish(void) { ((void (*)(void))FPL_PUBLISH)(); }
static void sleep_ms(uint32_t n) { ((void (*)(uint32_t))FPL_SLEEP)(n); }

/* {exinf, TA_HLNG|TA_DSNAME, entry, priority, stack, "LFPF", "IP", 0} */
static int32_t task_create(uintptr_t entry) {
    volatile uint32_t d[8];
    int32_t id;
    d[0] = 0; d[1] = 0x41u; d[2] = (uint32_t)entry; d[3] = FPL_TASK_PRIORITY;
    d[4] = FPL_TASK_STACK; d[5] = 0x4650464Cu; d[6] = 0x00005049u; d[7] = 0;
    id = ((int32_t (*)(volatile uint32_t *))FPL_TASK_CREATE)(d);
    if (id < 1) return id;
    return ((int32_t (*)(int32_t, uint32_t))FPL_TASK_START)(id, 0) == 0 ? id : -1;
}

/* Same fp L volume and file services as the card loader and fpl_putfile.
 * The short pathname is built locally: the resident unit has no .rodata. */
static uint32_t prefs_io(struct fpl_card *c, uint32_t *record, uint32_t writing) {
    volatile uint32_t path[3];
    path[0]=0x4650465cu; path[1]=0x2e50494cu; path[2]=0x00474643u; /* \FPFLIP.CFG */
    void *o=c->prefs_file;
    uintptr_t mgr=((uintptr_t (*)(void))0xC04408B8u)();
    if (!mgr) return 0;
    uintptr_t vol=((uintptr_t (*)(uintptr_t,uint32_t))0xC04408F8u)(mgr,0);
    ((void (*)(void *,uintptr_t))0xC03660A8u)(o,vol);
    uint32_t ok=((uint32_t (*)(void *,const volatile void *,uint32_t))0xC03661C8u)
        (o,path,writing ? 7u : 1u);
    if (ok) {
        uint32_t bytes=FPF_SETTINGS_WORDS*sizeof(*record), got=0;
        if (writing)
            ok=((uint32_t (*)(void *,const void *,uint32_t))0xC0366300u)(o,record,bytes);
        else
            ok=((uint32_t (*)(void *,void *,uint32_t,uint32_t *))0xC0366278u)
                (o,record,bytes,&got) && got==bytes;
        ((void (*)(void *))0xC0366238u)(o);
    }
    ((void (*)(void *,uint32_t))0xC03660E8u)(o,2u);
    return ok;
}

static void prefs_load(struct fpl_card *c) {
    uint32_t record[FPF_SETTINGS_WORDS];
    uint8_t table[FPF_SLOTS];
    if (prefs_io(c,record,0) && fpf_record_decode(record,table))
        for (unsigned i=0;i<FPF_SLOTS;++i)
            c->flip.state.table[i]=c->flip.state.saved[i]=table[i];
}

static uint32_t p_store(void *product, const uint8_t table[FPF_SLOTS]) {
    struct fpl_card *c=product;
    uint32_t record[FPF_SETTINGS_WORDS], back[FPF_SETTINGS_WORDS];
    fpf_record_encode(table,record);
    if (!prefs_io(c,record,1) || !prefs_io(c,back,0)) return 0;
    for (unsigned i=0;i<FPF_SETTINGS_WORDS;++i) if (record[i]!=back[i]) return 0;
    return 1;
}
static void p_queue(void *product, uint32_t value) {
    struct fpl_card *c = product;
    post_int(0u, (const char *)c->name, value);
}

/* ---- the row was written (UI thread) ------------------------------------- */
USED uint32_t fpl_changed(uintptr_t descriptor, struct fpl_card *c) {
    struct fplflip *p;
    uint32_t value, editing, set, key;
    if (!c || c->magic != CARD_MAGIC || !descriptor || (descriptor & 3u)) return 0;
    p = &c->flip;
    value = peek(descriptor + DESC_VALUE);
    editing = p->ops.mode.editing(p->ops.user);
    set = fpf_set_in_use(p->ops.mode.cinema(p->ops.user), p->ops.mode.style(p->ops.user));
    key = fpf_editing_key(editing, set);
    if (key == FPF_NO_KEY || value >= FPF_VALUES) return 0;
    return fpf_changed(&p->state, key, value);
}

/* ---- the table as saved in this file ------------------------------------ */
static uint32_t record_check(uint32_t lo, uint32_t hi) {
    return FPF_SETTINGS_MAGIC ^ 1u ^ lo ^ hi ^ 0xFFFFFFFFu;
}

static void settings_read(struct fpl_card *c, uintptr_t base) {
    const volatile uint32_t *r = (const volatile uint32_t *)(base + FPF_SETTINGS_OFF);
    uint32_t ok = r[0] == FPF_SETTINGS_MAGIC && r[1] == 1u && r[4] == record_check(r[2], r[3]);
    for (uint32_t n = 0; ok && n < FPF_SLOTS; ++n)
        ok = ((n < 4u ? r[2] : r[3]) >> (8u * (n & 3u)) & 0xFFu) < FPF_VALUES;
    for (uint32_t n = 0; n < FPF_SLOTS; ++n) {
        uint8_t v = ok ? (uint8_t)((n < 4u ? r[2] : r[3]) >> (8u * (n & 3u))) : 0u;
        c->flip.state.table[n] = v;
        c->flip.state.saved[n] = v;
    }
}

/* ---- load ---------------------------------------------------------------- */
static uint32_t aligned(uintptr_t a) { return a && !(a & 3u); }

static uint32_t same(uintptr_t a, const char *s) {
    if (!a) return 0;
    for (uint32_t n = 0;; ++n) {
        uint32_t ch = *(const volatile uint8_t *)(a + n);
        if (ch != (uint8_t)s[n]) return 0;
        if (!ch) return 1;
    }
}

/* ---- the UI app, ready only once it exists ------------------------------- *
 * The v1 boot hung in the first AutoRun echo: fpl_card_init dereferenced the
 * UI pointers the way a fully-booted camera shows them, and at entry time
 * they were not that yet.  v2 validated every word and waited; v3 drops the
 * wait entirely (a wrong sleep convention would hang the first echo all by
 * itself): one validated look, and if the UI is not there this instant the
 * sup skips the row and the camera boots on at normal speed, with the shell
 * alive and the state words telling the story over USB.  The install itself
 * still runs here, on the loader's task, as the uishare convention requires
 * (no concurrent writer against the UI thread). */
static uint32_t ui_ready(struct fpl_card *c) {
    uintptr_t app, regp, map;
    uint32_t count;
    app = peek(GUI_OBJECT);
    c->app_word = (uint32_t)app;
    if (!aligned(app) || app < 0xC3000000u || app >= 0xD0000000u) return 0;
    regp = peek(app + APP_REGISTRY);
    if (!aligned(regp) || regp < 0xC3000000u || regp >= 0xD0000000u) return 0;
    /* the registry's embedded name map, as fpl_ui's live snapshot validated:
     * +0x14 count, +0x18/+0x1c two heap pointers */
    map = regp + REGISTRY_OFF + 0x14u;
    count = peek(map + 8u);
    if (!count || count > 4096u) return 0;
    if (!aligned(peek(map + 12u)) || peek(map + 12u) < 0xC3000000u ||
        peek(map + 12u) >= 0xD0000000u) return 0;
    if (!aligned(peek(map + 16u)) || peek(map + 16u) < 0xC3000000u ||
        peek(map + 16u) >= 0xD0000000u) return 0;
    return 1;
}

/* The registered integer's descriptor, or 0. */
static uintptr_t variable(uintptr_t app, const char *NAME) {
    uintptr_t d = 0;
    if (lookup(peek(app + APP_REGISTRY) + REGISTRY_OFF, NAME, &d) != 0 || !aligned(d)) return 0;
    if (peek(d + DESC_TYPE) != VARIABLE_INT || !same(peek(d + DESC_NAME), NAME)) return 0;
    return d;
}

static uint32_t refuse(struct fpl_card *c, uint32_t why) { if (c) c->result = why; return 1; }
static uint32_t stop(struct fpl_card *c, uint32_t why) { c->result = why; return 0; }

/* The NBU reader chain, as uis_reader follows it (ui_pool.c): the reader
 * must exist and point at the right NBU base.  This initializes AFTER the
 * registry (which ui_ready checks), so the entry checks it separately and
 * defers the row install to the task when it isn't ready yet (2026-11-18,
 * live: NO_READER at entry time, valid 25ms later). */
static uint32_t reader_ready(void) {
    uintptr_t app, screens, screen, reader;
    uint32_t count;
    app = peek(GUI_OBJECT);
    if (!aligned(app) || app < 0xC3000000u || app >= 0xD0000000u) return 0;
    count = peek(app + 0x80u);
    screens = peek(app + 0x8Cu);
    if (!count || count > 512u || !aligned(screens)) return 0;
    screen = peek(screens);
    if (!aligned(screen)) return 0;
    reader = peek(screen + 0x24u);
    if (!aligned(reader)) return 0;
    if (peek(reader + 0x24u) != 0xC18D01A0u) return 0;   /* NBU base */
    return 1;
}

USED uint32_t fpl_card_init(struct fpl_card *c, uintptr_t base, const char *path) {
    volatile uint32_t *w;
    const char *NAME;
    struct fplflip_live live;
    uintptr_t app;
    if (!c || ((uintptr_t)c & 7u) || c->magic || c->result || !c->ui_at || !c->ui_len ||
        !c->task_entry || !c->changed_entry)
        return refuse(c, FPL_INVALID);
    c->magic = CARD_MAGIC;
    c->base = base;
    c->task_id = -1;
    w = c->name;                                    /* MV_fpScreenFlip; .text only */
    w[0] = 0x665F564Du; w[1] = 0x72635370u; w[2] = 0x466E6565u; w[3] = 0x0070696Cu; w[4] = 0u;
    NAME = (const char *)c->name;
    if (!firmware_ok()) return refuse(c, FPL_FIRMWARE);
    live.layer_register = FPL_LAYER_REG;
    live.touch_flag = FPL_TOUCH_FLAG;
    live.touch_vtable_at = FPL_TOUCH_VTABLE_AT;
    live.touch_vtable = FPL_TOUCH_VTABLE;
    live.editing_custom = FPL_EDITING_CUSTOM;
    live.status_object = FPL_STATUS_OBJECT;
    live.recording_offset = FPL_RECORDING_OFF;
    if (fplflip_init(&c->flip, &live, c, p_store, p_queue) != FPLFLIP_OK)
        return refuse(c, FPL_PROFILE);
    settings_read(c, base);
    c->self_path = 0;
    (void)path;
    c->install_polls = 0;
    /* One validated look, no waiting: if the UI is not there this instant the
     * row is skipped and the boot runs at normal speed. */
    if (!ui_ready(c)) return stop(c, FPL_NO_GUI);
    app = peek(GUI_OBJECT);
    {
        uintptr_t d = variable(app, NAME);
        if (!d) {
            uint32_t def[3];
            def[0] = VARIABLE_INT;
            def[1] = (uint32_t)(uintptr_t)NAME;
            def[2] = 0u;
            if (reg(app, 1, def) != 0) return refuse(c, FPL_REGISTER);
            c->registered = 1;
            d = variable(app, NAME);
        }
        if (!d) return stop(c, FPL_VARIABLE);
        /* v12: the descriptor is kept and the task POLLS its value word;
         * subscribe's callback-pool + registry work is what froze the
         * camera when it ran outside the firmware's own contexts
         * (2026-11-18, the gated stepper's release 9).  This lookup is
         * the live-proven one. */
        c->desc_word = (uint32_t)d;
    }
    publish();
    /* v11: the entry does ONLY what v6-v8 proved safe on the echo task
     * (allocate, copy, profile init, registry register).  The uia_apply +
     * subscribe install runs ONCE from the task, 2 s after boot (the boot is
     * fully settled then), never in a retry loop -- and a step word before
     * every risky call makes a freeze readable post-mortem over the shell.
     * v10 froze somewhere in the entry-side install; v9's task loop froze
     * dereferencing the pre-fix transposed address.  This shape isolates the
     * one unproven step (uia_apply from a resident task) with diagnostics. */
    c->install = 0;                                /* pending: the task installs */
    c->task_id = task_create(c->task_entry);
    if (c->task_id < 1) return stop(c, FPL_TASK);
    c->result = FPL_DEFERRED;
    return 0;
}

/* A task cannot end in this firmware.  The rotation pass runs inside the
 * poll (see profile.c); FPL_POLL_MS leaves the camera the rest of each
 * period.  The row install runs ONCE here, 2 s after boot (80 polls): by
 * then the boot is fully settled.  The step word (install) is written
 * before every risky call, so a freeze inside the install is readable
 * post-mortem over the shell:
 *   0x100 = before uia_apply, 0x101 = uia_apply returned,
 *   0x103 = installed (READY). */
#define FPL_INSTALL_DELAY_POLLS 80u
#define FPL_STEP_APPLY   0x100u
#define FPL_STEP_APPLIED 0x101u
#define FPL_STEP_DONE    0x103u

/* Experiment A (2026-11-18): the task, 2 s after boot, performs ONLY the
 * pool-copy allocation uia_apply would make (~192 KB), recording the result
 * and touching NO shared structure.  This bisects the v9/v10/v11 freeze:
 * those builds froze the camera (killing even the worker's USB task) the
 * first time uia_apply proceeded past the reader check with the corrected
 * GUI_OBJECT -- so the freeze is inside its real work.  Step words:
 *   0x200 = before the alloc, 0x201 = alloc returned (buf in app_word). */
#ifdef FPL_EXPERIMENT_A
uint32_t fplflip_probe_alloc(uint32_t bytes);
#endif

/* Experiment B (2026-11-18): the host-gated stepper.  v9/v10/v11 all froze
 * the whole camera the first time uia_apply's real work ran (the intern
 * path past its reader check, corrected GUI_OBJECT), and Experiment A
 * cleared the 192 KB allocation.  The task now calls the REAL uia_apply
 * with the stepper armed: ui_pool.c/ui_apply.c record a step number before
 * every risky step and wait for the host to release it over the shell, so
 * one boot walks the install one step at a time with the state readable
 * between steps.  Step words: 0x300 = called, 0x301 = reader+pool ok,
 * 0x302/0x304/0x306 = alloc/copy/header gates, 0x308 = THE pool switch,
 * 0x30a = the length bump, 0x30c = the ops, 0x30e = the page entry switch,
 * 0x310 = subscribe, 0x311 = READY.  If the camera dies after a release,
 * that step is the killer; if every gate passes, the row is installed. */
#ifdef FPL_EXPERIMENT_B
/* The gate addresses travel in struct uia_outcome (no writable data in
 * the sup's text: the builder refuses it); see ui_apply.h. */
#endif

USED void fpl_task(struct fpl_card *c) {
    for (;;) {
        if (c && c->magic == CARD_MAGIC) {
            if (c->result == FPL_DEFERRED) {
                c->install_polls++;
                if (c->install_polls == FPL_INSTALL_DELAY_POLLS) {
#ifdef FPL_EXPERIMENT_A
                    uintptr_t buf;
                    c->install = 0x200;
                    buf = fplflip_probe_alloc(16u + 176152u + 0x4000u);
                    c->app_word = (uint32_t)buf;
                    c->install = 0x201;
                    c->result = FPL_DEFERRED;      /* stays deferred: no install */
#elif defined(FPL_EXPERIMENT_B)
                    struct uia_outcome ui;
                    c->install = 0x300u;           /* uia_apply called; the gates are inside */
                    ui.b_step = (volatile uint32_t *)&c->install;
                    ui.b_gate = &c->gate;
                    c->gate = 0;
                    c->ui_result = uia_apply(c->ui_at, c->ui_len, &ui);
                    c->ui_op = ui.op;
                    if (c->ui_result == UIA_OK) {
                        struct fplflip *p = &c->flip;
                        c->first_id = ui.first_id;
                        c->row_y = ui.n_slots ? ui.slots[0] : 0;
                        c->page = ui.page;
                        /* v13: park BEFORE the poll.  The mode ops are raw
                         * firmware calls (profile.c DISPLAY/CINEMA/STYLE)
                         * that have never run live; the host restores them
                         * one per gate, sampling between each. */
                        c->exp_ops[0] = (uintptr_t)p->ops.mode.display;
                        c->exp_ops[1] = (uintptr_t)p->ops.mode.cinema;
                        c->exp_ops[2] = (uintptr_t)p->ops.mode.style;
                        c->exp_ops[3] = (uintptr_t)p->ops.mode.editing;
                        p->ops.mode.display = fplflip_stub_display;
                        p->ops.mode.cinema = fplflip_stub_mode;
                        p->ops.mode.style = fplflip_stub_mode;
                        p->ops.mode.editing = fplflip_stub_editing;
                        publish();
                        c->install = 0x310u;       /* installed; parked pre-poll */
                        /* result stays DEFERRED: the poll must not start */
                    } else {
                        c->result = FPL_UI;    /* recorded, no retry */
                    }
#else
                    struct uia_outcome ui;
                    c->install = FPL_STEP_APPLY;
                    c->ui_result = uia_apply(c->ui_at, c->ui_len, &ui);
                    c->ui_op = ui.op;
                    c->install = FPL_STEP_APPLIED;
                    if (c->ui_result == UIA_OK) {
                        c->first_id = ui.first_id;
                        c->row_y = ui.n_slots ? ui.slots[0] : 0;
                        c->page = ui.page;
                        /* v12: no subscribe -- the task watches the value
                         * word.  The initial read seeds last_value so the
                         * poll acts only on CHANGES. */
                        c->last_value = c->desc_word ?
                                        peek(c->desc_word + DESC_VALUE) : 0xFFFFFFFFu;
                        publish();
                        c->install = FPL_STEP_DONE;
                        c->result = FPL_READY;
                    } else {
                        c->result = FPL_UI;    /* recorded, no retry */
                    }
#endif
                }
#ifdef FPL_EXPERIMENT_B
                /* v13: the host's gate 9 starts the (stubbed) poll */
                if (c->install == 0x310u && c->gate >= 9u) {
                    c->last_value = c->desc_word ?
                                    peek(c->desc_word + DESC_VALUE) : 0xFFFFFFFFu;
                    publish();
                    c->result = FPL_READY;
                }
#endif
            }
            if (c->result == FPL_READY) {
#ifdef FPL_EXPERIMENT_B
                /* v13: the op ladder -- the host restores one mode op per
                 * gate; the state is sampled between each release. */
                if (c->gate >= 10u && c->install == 0x310u) {
                    c->flip.ops.mode.editing =
                        (uint32_t (*)(void *))(uintptr_t)c->exp_ops[3];
                    c->install = 0x311u;
                } else if (c->gate >= 11u && c->install == 0x311u) {
                    c->flip.ops.mode.cinema =
                        (uint32_t (*)(void *))(uintptr_t)c->exp_ops[1];
                    c->flip.ops.mode.style =
                        (uint32_t (*)(void *))(uintptr_t)c->exp_ops[2];
                    c->install = 0x312u;
                } else if (c->gate >= 12u && c->install == 0x312u) {
                    c->flip.ops.mode.display =
                        (uint32_t (*)(void *))(uintptr_t)c->exp_ops[0];
                    c->install = 0x313u;
                }
#endif
                /* v12: the polling twin of fpl_changed (nothing subscribes
                 * any more).  An aligned word read of the descriptor races
                 * nothing; the guards are the ones fpl_changed applies. */
                if (!c->flip.state.polls) prefs_load(c);
                uint32_t v = c->desc_word ? peek(c->desc_word + DESC_VALUE) : 0u;
                if (v != c->last_value) {
                    struct fplflip *p = &c->flip;
                    uint32_t editing = p->ops.mode.editing(p->ops.user);
                    uint32_t set = fpf_set_in_use(p->ops.mode.cinema(p->ops.user),
                                                  p->ops.mode.style(p->ops.user));
                    uint32_t key = fpf_editing_key(editing, set);
                    c->last_value = v;
                    if (key != FPF_NO_KEY && v < FPF_VALUES)
                        fpf_changed(&p->state, key, v);
                }
                fplflip_poll(&c->flip);
#ifdef FPL_ROW_BENCH
                if (c->flip.state.polls == 120 && !c->flip.engine.want)
                    fplflip_row_bench(&c->flip, c->bench);
#endif
            }
        }
        /* v16: while the engine runs, pace near the sensor's ~30 fps; the
         * idle poll keeps the quiet 25 ms convention. */
        sleep_ms(c && c->magic == CARD_MAGIC && c->flip.engine.on ? 15u : FPL_POLL_MS);
    }
}
