#include "profile.h"

/* fp L 3.02 addresses from screenflip_fpl/bindings.py.  Each getter is checked
 * before use.  Host tests replace the calls and the two memory helpers. */

#ifdef FPLFLIP_HOST_TEST
extern uint32_t fplflip_host_read(uintptr_t address);
extern void fplflip_host_write(uintptr_t address, uint32_t value);
extern uintptr_t fplflip_host_settings(void);
extern uintptr_t fplflip_host_display(uintptr_t settings);
extern uintptr_t fplflip_host_cinema(uintptr_t settings);
extern uintptr_t fplflip_host_style(uintptr_t settings);
extern void fplflip_host_relayout(uint32_t message);
extern uintptr_t fplflip_host_status(void);
#define READ(a)     fplflip_host_read((uintptr_t)(a))
#define READ8(a)    (fplflip_host_read((uintptr_t)(a)) & 0xFFu)
#define WRITE(a, v) fplflip_host_write((uintptr_t)(a), (uint32_t)(v))
#define SETTINGS()  fplflip_host_settings()
#define DISPLAY(s)  fplflip_host_display((s))
#define CINEMA(s)   fplflip_host_cinema((s))
#define STYLE(s)    fplflip_host_style((s))
#define RELAYOUT()  fplflip_host_relayout(0u)
#define STATUS()    fplflip_host_status()
#else
#define READ(a)     (*(volatile const uint32_t *)(uintptr_t)(a))
#define READ8(a)    (*(volatile const uint8_t *)(uintptr_t)(a))
#define WRITE(a, v) (*(volatile uint32_t *)(uintptr_t)(a) = (uint32_t)(v))
/* The mode getters are MEMBER calls taking the settings object -- the fp's
 * camera-proven pattern (C0057AE8() -> C0061BA8(s)/C0058340(s)/C0061A68(s));
 * the fp L twins are 0xC0055120() and these three.  Called without the
 * argument they dereference garbage and abort the whole system
 * (2026-11-18, the v13 ladder's release 11).  0xC0055120 is
 * disassembly-verified: zero-arg, returns the fixed object 0xC31AB100
 * after an init check. */
#define SETTINGS()  ((uintptr_t (*)(void))0xC0055120u)()
#define DISPLAY(s)  ((uintptr_t (*)(uintptr_t))0xC005F1E0u)(s)
#define CINEMA(s)   ((uintptr_t (*)(uintptr_t))0xC0055978u)(s)
#define STYLE(s)    ((uintptr_t (*)(uintptr_t))0xC005F0A0u)(s)
#define RELAYOUT()  ((void (*)(uint32_t))0xC02E8468u)(0u)
#define STATUS()    ((uintptr_t (*)(void))0xC00178D8u)()
#endif

#define WORDS(X) \
    X(0xC0055120u, 0xE92D4070u)  /* settings object */ \
    X(0xC005F1E0u, 0xE92D4030u)  /* display mode */ \
    X(0xC0055978u, 0xE92D4030u)  /* cinema mode */ \
    X(0xC005F0A0u, 0xE92D4030u)  /* shooting style */ \
    X(0xC02E8468u, 0xE92D4010u)  /* relayout */ \
    X(0xC00178D8u, 0xE92D4010u)  /* system status object */ \
    X(0xC001D740u, 0xE92D44F0u)  /* allocator request (live 2026-11-18) */ \
    X(0xC001D7F0u, 0xE1A0C000u)  /* allocator result (live) */ \
    X(0xC001D7A0u, 0xE92D4030u)  /* allocator release (live) */ \
    X(0xC000E91Cu, 0xE10F2000u)  /* D-cache clean+invalidate */ \
    X(0xC000E9B8u, 0xE10F2000u)  /* D-cache clean only (set/way) */ \
    X(0xC000EB28u, 0xE10FC000u)  /* D-cache invalidate by MVA: (start, end) */ \

/* Engine natives.  Host tests substitute the allocator and the cache call;
 * the ring/registers go through READ/WRITE like every other binding. */
#ifdef FPLFLIP_HOST_TEST
extern uintptr_t fplflip_host_alloc(uint32_t bytes);
extern void fplflip_host_release(uint32_t *desc);
extern void fplflip_host_clean(void);
static uintptr_t engine_alloc(uint32_t desc[4], uint32_t bytes) {
    (void)desc; return fplflip_host_alloc(bytes);
}
static void engine_release(uint32_t desc[4]) { fplflip_host_release(desc); }
static void cache_sync(void) { fplflip_host_clean(); }
#else
static uintptr_t engine_alloc(uint32_t desc[4], uint32_t bytes) {
    typedef void (*get_fn)(uint32_t *, uint32_t, uint32_t, uint32_t);
    typedef uintptr_t (*addr_fn)(uint32_t *);
    ((get_fn)0xC001D740u)(desc, 0u, bytes, 0u);
    return ((addr_fn)0xC001D7F0u)(desc);
}
static void engine_release(uint32_t desc[4]) {
    ((void (*)(uint32_t *, uint32_t))0xC001D7A0u)(desc, 2u);
}
/* v15: the per-pass full clean+invalidate (0xC000E91C) is gone; the engine
 * publishes with clean-only (0xC000E9B8) and refreshes the ring slot by
 * MVA (0xC000EB28).  The routine stays fingerprinted in WORDS(). */
#endif

uint32_t fplflip_firmware_ok(void) {
#ifdef FPLFLIP_HOST_TEST
    return 1u;
#else
#define CHECK(at, word) if (READ(at) != (word)) return 0u;
    WORDS(CHECK)
#undef CHECK
    return 1u;
#endif
}

/* The settings object for this poll's mode reads; 0 when it is not the
 * expected kind of pointer, and the getters answer safe values then. */
static uintptr_t settings_now(void) {
    uintptr_t s = SETTINGS();
    return (s >= 0xC0000000u && s < 0xC8000000u && !(s & 3u)) ? s : 0u;
}
static uint32_t get_display(void *user) {
    (void)user;
    uintptr_t s = settings_now();
    return s ? (uint32_t)DISPLAY(s) : FPF_CUSTOMS;
}
static uint32_t get_cinema(void *user) {
    (void)user;
    uintptr_t s = settings_now();
    return s ? (uint32_t)CINEMA(s) : 0u;
}
static uint32_t get_style(void *user) {
    (void)user;
    uintptr_t s = settings_now();
    return s ? (uint32_t)STYLE(s) : 1u;
}
static uint32_t get_editing(void *user) {
    const struct fplflip *p = user;
    uint32_t value = READ(p->live.editing_custom);
    return value < FPF_CUSTOMS ? value : FPF_NO_KEY;
}
static uint32_t is_recording(void *user) {
    const struct fplflip *p = user;
    uintptr_t status;
    if (!p->live.status_object) return 1u;       /* unknown: assume recording */
    status = STATUS();
    if (status < 0xC0000000u || status > 0xC8000000u || (status & 3u)) return 1u;
    return READ8(status + p->live.recording_offset);
}
static uint32_t bridge_store(void *user, const uint8_t table[FPF_SLOTS]) {
    struct fplflip *p = user;
    return p->store ? p->store(p->product, table) : 0u;
}
static void bridge_queue(void *user, uint32_t value) {
    struct fplflip *p = user;
    if (p->queue) p->queue(p->product, value);
}

uint32_t fplflip_init(struct fplflip *p, const struct fplflip_live *live,
                      void *product,
                      uint32_t (*write_store)(void *product, const uint8_t table[FPF_SLOTS]),
                      void (*write_queue)(void *product, uint32_t value)) {
    if (!p || !live || !write_store || !write_queue) return FPLFLIP_ARGUMENT;
    if (!live->layer_register || !live->touch_flag || !live->touch_vtable_at ||
        !live->touch_vtable || !live->editing_custom ||
        (live->layer_register & 3u) || (live->touch_flag & 3u) ||
        (live->touch_vtable_at & 3u) || (live->editing_custom & 3u))
        return FPLFLIP_LIVE;
    if (live->layer_register < 0x20000000u || live->layer_register >= 0x40000000u)
        return FPLFLIP_LIVE;
    if (live->touch_flag < 0xC0000000u || live->touch_flag >= 0xC8000000u ||
        live->touch_vtable_at < 0xC0000000u || live->touch_vtable_at >= 0xC8000000u ||
        live->editing_custom < 0xC0000000u || live->editing_custom >= 0xC8000000u ||
        (live->status_object && (live->status_object < 0xC0000000u ||
                                 live->status_object >= 0xC8000000u)))
        return FPLFLIP_LIVE;
    if (!fplflip_firmware_ok()) return FPLFLIP_FIRMWARE;
    p->live = *live;
    p->product = product;
    p->store = write_store;
    p->queue = write_queue;
    p->engine.want = 0u;
    p->engine.on = 0u;
    p->engine.buf[0] = p->engine.buf[1] = 0u;
    p->engine.flip = FPLFLIP_UNPINNED;
    p->engine.src = 0u;
    p->engine.fails = 0u;
    p->overlay.buf = 0;
    p->engine.passes = p->engine.last_ticks = p->engine.use_stock_copy = p->engine.diag = 0u;
#if !defined(FPLFLIP_HOST_TEST) && !defined(FPL_ROW_BENCH)
    p->engine.use_stock_copy = READ(0xC001510Cu)==0xe92d4141u &&
                               READ(0xC0015160u)==0xe8b15048u;
#endif
    p->engine.desc[0][0] = p->engine.desc[0][1] = p->engine.desc[0][2] = p->engine.desc[0][3] = 0u;
    p->engine.desc[1][0] = p->engine.desc[1][1] = p->engine.desc[1][2] = p->engine.desc[1][3] = 0u;
    p->ops.panel = (struct fpf_panel){0}; /* picture-only profile */
    p->ops.mode.display = get_display;
    p->ops.mode.cinema = get_cinema;
    p->ops.mode.style = get_style;
    p->ops.mode.editing = get_editing;
    p->ops.recording = is_recording;
    p->ops.store = bridge_store;
    p->ops.queue = bridge_queue;
    p->ops.user = p;
    fpf_state_init(&p->state);
    return FPLFLIP_OK;
}

/* ---- the rotation engine --------------------------------------------------
 * One private class-0 buffer, the display's four fetch bases pinned to it,
 * one ring source rotated into it per poll (2026-11-18: the host-driven POC
 * was watched live and called perfect, but it rotated HOST-side over the
 * shell's DMA path, which touches no CPU cache; the native loop must do the
 * cache work itself, so v15 does it surgically: refresh only the ring slot
 * being read by MVA, publish clean-only, and never sweep the whole cache --
 * two full clean+invalidate sweeps per pass evicted every task's working
 * set 76x/s and made the whole camera crawl).  Never hold the allocator across
 * recording: the pass tears down while recording and starts again after. */
static void engine_pin(struct fplflip *p, uint32_t idx) {
    uint32_t base = (uint32_t)(p->engine.buf[idx] - 0x40000000u) >> 2;
    for (uint32_t i = 0; i < 4u; ++i) WRITE(FPLFLIP_FETCH_BASES + 4u * i, base);
    WRITE(FPLFLIP_FETCH_BASES, READ(FPLFLIP_FETCH_BASES));   /* re-arm */
}

static void engine_teardown(struct fplflip *p) {
    uint32_t i;
    if (!p->engine.on) return;
    for (i = 0; i < 4u; ++i) WRITE(FPLFLIP_FETCH_BASES + 4u * i, p->engine.bases[i]);
    WRITE(FPLFLIP_FETCH_BASES, READ(FPLFLIP_FETCH_BASES));
    for (i = 0; i < 2u; ++i) {
        if (p->engine.buf[i]) {
            engine_release(p->engine.desc[i]);
            p->engine.buf[i] = 0u;
            p->engine.desc[i][0] = p->engine.desc[i][1] = 0u;
            p->engine.desc[i][2] = p->engine.desc[i][3] = 0u;
        }
    }
    p->engine.on = 0u;
    p->engine.flip = FPLFLIP_UNPINNED;
}

static uint32_t engine_alloc_one(struct fplflip *p, uint32_t idx) {
    uintptr_t b = engine_alloc(p->engine.desc[idx], FPLFLIP_ENGINE_BYTES);
    if (!b || b < 0x44000000u || b >= 0x80000000u || (b & 63u)) {
        if (b) engine_release(p->engine.desc[idx]);
        p->engine.desc[idx][0] = p->engine.desc[idx][1] = 0u;
        p->engine.desc[idx][2] = p->engine.desc[idx][3] = 0u;
        p->engine.fails++;
        return 0u;
    }
    p->engine.buf[idx] = b;
    return 1u;
}

static uint32_t engine_start(struct fplflip *p) {
    uint32_t i;
    if (p->engine.on) return 1u;
    if (!p->engine.buf[0] && !engine_alloc_one(p, 0u)) return 0u;
    if (!p->engine.buf[1] && !engine_alloc_one(p, 1u)) {
        /* v19: refuse rather than tear.  One buffer means writing the frame
         * the display is scanning out (the v16-v18 "noise"). */
        p->engine.diag = 1u;
        engine_teardown(p);
        return 0u;
    }
    for (i = 0; i < 4u; ++i) p->engine.bases[i] = READ(FPLFLIP_FETCH_BASES + 4u * i);
    p->engine.on = 1u;
    /* v19: the display is NOT pinned here.  The first pass writes a whole
     * frame into buf[1] and the pin happens only when it is complete:
     * pinning before the first write showed the display 4 MB of
     * never-written memory for the whole first pass -- the "noise" at
     * every apply (2026-11-19).  UNPINNED marks "the display is not
     * reading ours yet". */
    p->engine.flip = FPLFLIP_UNPINNED;
    return 1u;
}

/* One YUYV line, 810 words: reversed word order, the two Y bytes of each
 * word swapped, U/V kept (a true 180 of the packed pair).  No division, no
 * float, whole-word moves only. */
/* v17 performance: the v16 loops used volatile single-word accesses, which
 * the compiler cannot merge -- 1.75M full-latency ops made a pass cost
 * ~440 ms (the user's "a frame every half second") while the memory system
 * itself moves a frame in ~9 ms (the shell's DMA measurement).  Now: plain
 * (non-volatile) pointers -- the ROM cache calls below are function-call
 * barriers, so ordering is safe -- and stores ASCENDING (dst index i) so
 * write-allocate sees a streaming full-line pattern; loads may descend
 * (a miss fetches the line either way).  ARM-only: the host model has no
 * memory and never calls them. */
#ifndef FPLFLIP_HOST_TEST
#include "yuyv.h"
#endif

/* v15 cache surgery (2026-11-18): the smooth POC rotated host-side over the
 * shell's DMA path, which involves no CPU cache.  The native loop's two
 * full clean+invalidate sweeps per pass (0xC000E91C) evicted every task's
 * working set 76x/s and made the whole camera crawl.  Now: refresh only
 * the ring slot being read -- the develop writes it by DMA, so only this
 * task's own cached views of it can be stale -- with the ROM's
 * invalidate-by-MVA (0xC000EB28, args start/end), and publish with the
 * ROM's clean-only set/way sweep (0xC000E9B8), which writes back my dirty
 * lines without invalidating anything.  Both entries carry the same
 * IRQ-masked preamble as the proven 0xC000E91C and are fingerprinted. */
#ifndef FPLFLIP_HOST_TEST
static void drop_ring_lines(uintptr_t from, uint32_t bytes) {
    ((void (*)(uintptr_t, uintptr_t))0xC000EB28u)(from, from + bytes);
}
static void clean_set_way(void) { ((void (*)(void))0xC000E9B8u)(); }
#else
static void clean_set_way(void) { cache_sync(); }    /* the host model: one publish */
#endif

#ifndef FPLFLIP_HOST_TEST
static void reverse_rows(uintptr_t src, uintptr_t dst, uint32_t first, uint32_t count,
                         uint32_t use_stock) {
    for (uint32_t row = first; row < first + count; ++row) {
        const uint32_t *s=(const uint32_t *)(src + (FPLFLIP_LINES - 1u - row) * FPLFLIP_STRIDE);
        uint32_t *d=(uint32_t *)(dst + row * FPLFLIP_STRIDE);
        if (use_stock)
            ((void *(*)(void *, const void *, uint32_t))0xC001510Cu)(d,s,FPLFLIP_STRIDE);
        else xform_copy(s,d);
    }
}
#endif

#ifdef FPL_ROW_BENCH
static uint32_t tick_us(void) {
    return ((uint32_t (*)(void))0xC0029320u)();
}
#endif

#ifndef FPLFLIP_HOST_TEST
static uint32_t alias_matches(uintptr_t a) {
    /* Two supersection views observed on 3.02. Check every covered MB,
     * including an image crossing the end of a supersection. */
    for (uint32_t m=a>>20; m<=((a+FPLFLIP_FRAME_BYTES-1)>>20); ++m) {
        uint32_t phys=(m<<20)&0xff000000u;
        if (READ(0x44300000u+4*m)!=(phys|0x44c02u) ||
            READ(0x44300000u+4*(m+0x800u))!=(phys|0x45c06u)) return 0;
    }
    return 1;
}
#endif

#ifdef FPL_ROW_BENCH
/* Two private images, no scanout, DMA writer, or live-ring race. The same
 * kernel and cache-chunk order as the live vertical-flip path. */
void fplflip_row_bench(struct fplflip *p, uint32_t r[24]) {
    uint32_t sd[4] = {0}, dd[4] = {0};
    const uint32_t words = FPLFLIP_FRAME_BYTES / 4u;
    uint32_t *s = 0, *d = 0;
    r[0] = 0x33574252u; /* RBW3: baseline, stock, baseline, stock, stock */
    r[1] = 1; r[2] = 0;
    if (READ(0xC0029320u) != 0xE92D4010u || READ(0xC001510Cu)!=0xe92d4141u ||
        READ(0xC0015160u)!=0xe8b15048u || p->ops.recording(p->ops.user)) return;
    uint32_t t = tick_us();
    ((void (*)(uint32_t))0xC03707F0u)(100u);
    r[3] = tick_us() - t;
    s = (uint32_t *)engine_alloc(sd, FPLFLIP_ENGINE_BYTES);
    if (!s || (uintptr_t)s < 0x44000000 || (uintptr_t)s > 0x80000000u-FPLFLIP_ENGINE_BYTES || ((uintptr_t)s & 63)) goto done;
    d = (uint32_t *)engine_alloc(dd, FPLFLIP_ENGINE_BYTES);
    if (!d || (uintptr_t)d < 0x44000000 || (uintptr_t)d > 0x80000000u-FPLFLIP_ENGINE_BYTES || ((uintptr_t)d & 63)) goto done;
    if ((uintptr_t)d < (uintptr_t)s+FPLFLIP_ENGINE_BYTES &&
        (uintptr_t)s < (uintptr_t)d+FPLFLIP_ENGINE_BYTES) goto done;
    if (!alias_matches((uintptr_t)s) || !alias_matches((uintptr_t)d)) goto done;
    for (unsigned k = 0; k < 5; ++k) {
        if (p->ops.recording(p->ops.user)) break;
        uint32_t salt = 0xc37d9921u ^ (k << 24);
        uintptr_t cs=(uintptr_t)s+0x80000000u, cd=(uintptr_t)d+0x80000000u;
        uintptr_t rs=cs, wd=(uintptr_t)d;
        drop_ring_lines(cs,FPLFLIP_FRAME_BYTES);
        drop_ring_lines(cd,FPLFLIP_FRAME_BYTES);
        for (unsigned i=0; i<words; ++i) { s[i] = salt ^ i; d[i] = 0; }
        clean_set_way();
        uint32_t inv=0, copy=0, start=tick_us();
        for (unsigned i=0; i<FPLFLIP_LINES; i+=64) {
            unsigned n=FPLFLIP_LINES-i < 64 ? FPLFLIP_LINES-i : 64;
            uint32_t a=tick_us();
            drop_ring_lines(rs+(FPLFLIP_LINES-i-n)*FPLFLIP_STRIDE, n*FPLFLIP_STRIDE);
            uint32_t b=tick_us();
            reverse_rows(rs,wd,i,n,k==1 || k>=3);
            __asm__ volatile ("" ::: "memory");
            inv+=b-a; copy+=tick_us()-b;
        }
        uint32_t a=tick_us(); clean_set_way(); uint32_t end=tick_us();
        r[4+4*k]=inv; r[5+4*k]=copy; r[6+4*k]=end-a; r[7+4*k]=end-start;
        for (unsigned y=0; y<FPLFLIP_LINES; ++y) {
            if (!(y%64)) {
                unsigned n=FPLFLIP_LINES-y < 64 ? FPLFLIP_LINES-y : 64;
                drop_ring_lines(cd+y*FPLFLIP_STRIDE,n*FPLFLIP_STRIDE);
            }
            for (unsigned x=0; x<FPLFLIP_WORDS; ++x)
                r[2] += d[y*FPLFLIP_WORDS+x] != (salt ^ ((FPLFLIP_LINES-1-y)*FPLFLIP_WORDS+x));
        }
        r[1]=2+k;
        if (r[2]) break;
        ((void (*)(uint32_t))0xC03707F0u)(25u);
    }
done:
    if (r[1]==6 && !r[2] && r[11]<r[7] && r[19]<r[15] && r[23]<r[15])
        p->engine.use_stock_copy=1; /* diagnostic build: only after the A/B passes */
    if (d) engine_release(dd);
    if (s) engine_release(sd);
}
#endif

#ifndef FPLFLIP_HOST_TEST
#define OSD_PLANE0 0x301901f0u
#define OSD_PLANE1 0x30190250u
#define OSD_PIXELS (1024u*682u)
#define OSD_PAIR_BYTES (3u*OSD_PIXELS)
_Static_assert(2u*OSD_PAIR_BYTES <= FPLFLIP_ENGINE_BYTES,"OSD buffer size");

static void osd_release(struct fplflip *p) {
    if (!p->overlay.buf) return;
    for (unsigned i=0; i<2; ++i) {
        uintptr_t reg=i ? OSD_PLANE1:OSD_PLANE0;
        if (p->overlay.shown[i] && READ(reg)==p->overlay.shown[i])
            WRITE(reg,p->overlay.source[i]);
    }
    if (READ(p->live.touch_vtable_at)==p->live.touch_vtable)
        WRITE(p->live.touch_flag,p->overlay.old_touch);
    __asm__ volatile ("dsb sy" ::: "memory");
    /* Keep the pixels allocated through a display refresh after unpinning. */
    ((void (*)(uint32_t))0xC03707F0u)(40);
    engine_release(p->overlay.desc);
    p->overlay.buf=0;
}

static void osd_poll(struct fplflip *p) {
    if (!p->engine.on ||
        (p->engine.want!=FPLFLIP_MODE_V && p->engine.want!=FPLFLIP_MODE_180) ||
        READ(p->live.touch_vtable_at)!=p->live.touch_vtable ||
        READ(OSD_PLANE0+0x10)!=256 || READ(OSD_PLANE1+0x10)!=512 ||
        (READ(OSD_PLANE0+0x38)&0x3fff)!=1024 || (READ(OSD_PLANE1+0x38)&0x3fff)!=1024 ||
        (READ(OSD_PLANE0+0x34)&0x7ff)!=682 || (READ(OSD_PLANE1+0x34)&0x7ff)!=682) {
        osd_release(p);
        return;
    }
    if (!p->overlay.buf) {
        for (unsigned i=0;i<4;++i) p->overlay.desc[i]=0;
        uintptr_t b=engine_alloc(p->overlay.desc,FPLFLIP_ENGINE_BYTES);
        if (!b) return;
        if (b<0x44000000u || b>0x80000000u-FPLFLIP_ENGINE_BYTES || (b&63u)) {
            engine_release(p->overlay.desc); return;
        }
        p->overlay.buf=b;
        p->overlay.old_touch=READ(p->live.touch_flag);
        for (unsigned i=0;i<2;++i) {
            p->overlay.shown[i]=0; p->overlay.source[i]=0; p->overlay.bank[i]=0;
        }
    }
    for (unsigned i=0; i<2; ++i) {
        uintptr_t reg=i ? OSD_PLANE1:OSD_PLANE0;
        uint32_t addr=READ(reg);
        if (addr==p->overlay.shown[i] && addr) continue; /* no new stock presentation */
        if (addr<0x01000000u || addr>0x0ff00000u) { osd_release(p);return; }
        uintptr_t src=0x40000000u+4u*addr;
        if (src>=p->overlay.buf && src<p->overlay.buf+FPLFLIP_ENGINE_BYTES) {
            osd_release(p);return;
        }
        uint32_t bank=p->overlay.bank[i]^1u;
        uintptr_t dst=p->overlay.buf + bank*OSD_PAIR_BYTES + i*OSD_PIXELS;
        turn_osd((const uint32_t *)src,(uint32_t *)dst,(i+1)*OSD_PIXELS/4,!i);
        __asm__ volatile ("dsb sy" ::: "memory");
        if (READ(reg)!=addr) continue; /* firmware presented a newer surface */
        p->overlay.source[i]=addr;
        p->overlay.bank[i]=bank;
        p->overlay.shown[i]=(dst-0x40000000u)/4;
        WRITE(reg,p->overlay.shown[i]);
    }
    if (p->overlay.shown[1]) WRITE(p->live.touch_flag,1u);
}
#endif

static void engine_pass(struct fplflip *p) {
    uintptr_t src, dst;
    uint32_t mine, i, back;
    if (!p->engine.want || p->ops.recording(p->ops.user)) {
        engine_teardown(p);
        return;
    }
    if (!engine_start(p)) return;
    if (p->engine.flip != FPLFLIP_UNPINNED) {       /* a relayout rewrote them */
        mine = (uint32_t)(p->engine.buf[p->engine.flip] - 0x40000000u) >> 2;
        for (i = 0; i < 4u; ++i)
            if (READ(FPLFLIP_FETCH_BASES + 4u * i) != mine) { engine_pin(p, p->engine.flip); break; }
    }
    src = FPLFLIP_RING_FIRST + (uintptr_t)p->engine.src * FPLFLIP_RING_STRIDE;
    p->engine.src = (p->engine.src + 1u) & 3u;
    /* v19: the back buffer is the one the display is NOT reading.  Before
     * the first pin (UNPINNED) that is buf[1]; the pin happens ONLY after
     * a complete frame is written, so the display never scans a half or
     * never-written buffer (v16-v18's noise and tearing). */
    back = p->engine.flip == FPLFLIP_UNPINNED ? 1u : (p->engine.flip ^ 1u);
    dst = p->engine.buf[back];
#ifndef FPLFLIP_HOST_TEST
    /* The source-only cached pass beat both cached and direct on the camera.
     * Keep dst in the allocator's original view for display handoff. */
    if (alias_matches(src)) src += 0x80000000u;
    /* v17: iterate DESTINATION lines ascending (write-allocate streaming),
     * invalidate the ring a 64-line chunk at a time just before reading it
     * (the ROM routine masks IRQs for the whole range: one 3.5 MB call was
     * ~4 ms of masked IRQs 40x/s -- enough to starve the USB worker; a
     * 64-line chunk is ~0.1 ms). */
    for (i = 0; i < FPLFLIP_LINES; i += 64u) {
        uint32_t n = FPLFLIP_LINES - i < 64u ? FPLFLIP_LINES - i : 64u;
        /* v18: invalidate the lines this chunk READS.  H reads ascending
         * from i; 180/V read the DESCENDING end of the frame (v17
         * invalidated the write range instead, so those modes read stale
         * cached lines: the previous frame bled into the new one -- the
         * "lots of noise" in both 180 modes, while mirror stayed clean). */
        uint32_t lo = p->engine.want == FPLFLIP_MODE_H ? i : FPLFLIP_LINES - i - n;
        drop_ring_lines(src + lo * FPLFLIP_STRIDE, n * FPLFLIP_STRIDE);
        if (p->engine.want == FPLFLIP_MODE_V) {
            reverse_rows(src,dst,i,n,p->engine.use_stock_copy);
            continue;
        }
        for (uint32_t j = 0; j < n; ++j) {
            const uint32_t *sl = (const uint32_t *)(src + (i + j) * FPLFLIP_STRIDE);
            uint32_t *dl = (uint32_t *)(dst + (i + j) * FPLFLIP_STRIDE);
            const uint32_t *rl = (const uint32_t *)(src + (FPLFLIP_LINES - 1u - (i + j)) * FPLFLIP_STRIDE);
            if (p->engine.want == FPLFLIP_MODE_H) {
                xform_word_rev(sl, dl);
            } else {                      /* 180: reversed source line, word-rev */
                xform_word_rev(rl, dl);
            }
        }
    }
#else
    (void)src; (void)dst;                 /* the host model has no memory */
#endif
    clean_set_way();                      /* the new frame is in DRAM */
    p->engine.passes++;                   /* v19: the host computes the rate */
    p->engine.flip = back;
    engine_pin(p, back);                  /* restart the scanout on it */
}

static uint32_t apply_picture(const struct fpf_panel *panel, void *user, uint32_t value) {
    (void)panel;
    struct fplflip *p = user;
    uint32_t mode = value == 1 ? FPLFLIP_MODE_180 : value == 2 ? FPLFLIP_MODE_H :
                    value == 3 ? FPLFLIP_MODE_V : 0;
    if (p->engine.want == mode) return FPF_APPLY_NONE;
    p->engine.want = mode;
    return FPF_APPLY_WRITTEN;
}

void fplflip_poll(struct fplflip *p) {
    if (!p) return;
    fpf_poll_using(&p->state, &p->ops, apply_picture);
    engine_pass(p);
#ifndef FPLFLIP_HOST_TEST
    osd_poll(p);
#endif
}

uint32_t fplflip_engine_on(const struct fplflip *p) {
    return p && p->engine.on ? 1u : 0u;
}

/* Experiment A (flip_fpl.c): the pool-copy allocation alone, from a task. */
#ifdef FPL_EXPERIMENT_A
uint32_t fplflip_probe_alloc(uint32_t bytes) {
    uint32_t desc[4] = {0, 0, 0, 0};
    uintptr_t buf = engine_alloc(desc, bytes);
    /* deliberately not released: the next boot resets everything anyway */
    return (uint32_t)buf;
}
#endif

/* Experiment B, v13 (flip_fpl.c): the mode-op ladder's stubs.  display
 * answers a mode that skips the panel apply; editing answers no key; the
 * raw getters (DISPLAY/CINEMA/STYLE above) have never run live. */
#ifdef FPL_EXPERIMENT_B
uint32_t fplflip_stub_display(void *user) { (void)user; return FPF_CUSTOMS; }
uint32_t fplflip_stub_mode(void *user) { (void)user; return 0u; }
uint32_t fplflip_stub_editing(void *user) { (void)user; return FPF_NO_KEY; }
#endif
