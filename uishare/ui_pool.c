#include "ui_pool.h"

/* Ver.5.02 runtime layout, read from the camera 2026-09-30:
 *   [C37B7048] = UI app; app+80 screen count, app+8C screen array
 *   screen+24  = the shared NBU reader (every screen has the same one)
 *   reader+10  = pool length, reader+14 = pool address, reader+24 = NBU base
 * fp L 3.02 (UIS_FPL_302): the same layout at GUI_OBJECT 0xC37B6B10 (the
 * corrected address, GUI_GET-proven 2026-11-18; the 2026-11-17 read used a
 * transposed 0xC3766B10) and NBU 0xC18D01A0, live-verified read-only by
 * fpl_ui.py.  The
 * reader/pool/intern path is the camera-proven one; the nested string
 * layers are EXCLUDED on fp L because their veneers were carved from the
 * fp cave, whose words are the IMU image's there (ui_apply.c UIA_FPL_302
 * takes the same line for its service probe). */
#define GUI_OBJECT    0xC37B6B10u
#define NBU_BASE      0xC18D01A0u
#define SCREENS_COUNT 0x80u
#define SCREENS_ARRAY 0x8Cu
#define SCREEN_READER 0x24u
#define READER_LEN    0x10u
#define READER_POOL   0x14u
#define READER_BASE   0x24u
#define MAX_SCREENS   512u

#if defined(UIS_HOST_TEST)
#define N (&uis_test_natives)
#define peek(a) N->read(a)
#define poke(a, v) N->write(a, v)
#define peek8(a) N->read_byte(a)
#define poke8(a, v) N->write_byte(a, v)
#define alloc(n) N->alloc(n)
#define publish() N->publish()
#define icache() N->icache()
#elif defined(__arm__) && UINTPTR_MAX == UINT32_MAX
#define peek(a) (*(volatile const uint32_t *)(a))
#define poke(a, v) (*(volatile uint32_t *)(a) = (v))
#define peek8(a) (*(volatile const uint8_t *)(a))
#define poke8(a, v) (*(volatile uint8_t *)(a) = (uint8_t)(v))
/* USER memory from the camera's allocator, as every sup launcher gets it:
 * C001D740(desc, class 0, bytes, 0), C001D7F0(desc) -> address. Never freed. */
static uintptr_t alloc(uint32_t n) {
    typedef void (*get_fn)(uint32_t *, uint32_t, uint32_t, uint32_t);
    typedef uintptr_t (*addr_fn)(uint32_t *);
    uint32_t desc[4] = {0, 0, 0, 0};
    ((get_fn)0xC001D740u)(desc, 0, n, 0);
    return ((addr_fn)0xC001D7F0u)(desc);
}
static void publish(void) { typedef void (*fn)(void); ((fn)0xC000E91Cu)(); }
static void icache(void) { typedef void (*fn)(void); ((fn)0xC000EABCu)(); }
#else
#error "ARM32 native UI ABI required; host tests must explicitly substitute it"
#endif

static uint32_t aligned(uintptr_t a) { return a && !(a & 3u); }

/* ---- Experiment B (FPL_EXPERIMENT_B): the host-gated stepper ----------------
 * The task calls uia_apply with out->b_step/b_gate pointing at the card's
 * state words; those addresses thread down as ARGUMENTS (the sup's text
 * carries no writable data -- the builder refuses a writable section).  The
 * intern path below records a step number before every risky step and waits
 * for the host to release it over the shell (mem set), sleeping between
 * checks so the shell's own task can run and answer.  A freeze therefore
 * names its step, and every step before it was observed live on a booted
 * camera.  Nothing here waits unless both addresses are nonzero. */
#ifdef FPL_EXPERIMENT_B
void uis_b_pause(uintptr_t step_at, uintptr_t gate_at, uint32_t step, uint32_t want) {
    volatile uint32_t *step_w = (volatile uint32_t *)step_at;
    volatile uint32_t *gate_w = (volatile uint32_t *)gate_at;
    uint32_t spins = 0u;
    if (!step_w || !gate_w) return;               /* not driven: run on */
    *step_w = step;
    /* v13: each gate also releases itself after ~60 ms, so the install runs
     * at the real ~2 s boot timing with no host connected (v12 froze there);
     * the host's word can still release it earlier. */
    while (*gate_w < want && spins++ < 12u)
        ((void (*)(uint32_t))0xC03707F0u)(5u);    /* the task loop's own sleep */
}
#define UIS_B_AT(step_at, gate_at, step, want) \
    uis_b_pause((step_at), (gate_at), (step), (want))
#else
#define UIS_B_AT(step_at, gate_at, step, want) \
    ((void)(step_at), (void)(gate_at))
#endif

uintptr_t uis_reader(void) {
    uintptr_t app = peek(GUI_OBJECT), screens, screen, reader;
    uint32_t count;
    if (!aligned(app)) return 0;
    count = peek(app + SCREENS_COUNT);
    screens = peek(app + SCREENS_ARRAY);
    if (!count || count > MAX_SCREENS || !aligned(screens)) return 0;
    screen = peek(screens);
    if (!aligned(screen)) return 0;
    reader = peek(screen + SCREEN_READER);
    if (!aligned(reader) || peek(reader + READER_BASE) != NBU_BASE) return 0;
    return reader;
}

static uint32_t ours(uintptr_t pool, uint32_t len) {
    return pool > UIS_HEADER && !(pool & 3u) &&
           peek(pool - 16) == UIS_MAGIC && peek(pool - 12) == UIS_VERSION &&
           peek(pool - 8) >= len && len >= UIS_STOCK_LEN;
}

uint32_t uis_pool_known(uintptr_t reader) {
    uintptr_t pool;
    uint32_t len;
    if (!reader) return 0;
    pool = peek(reader + READER_POOL);
    len = peek(reader + READER_LEN);
    return (pool == UIS_STOCK_POOL && len == UIS_STOCK_LEN) || ours(pool, len);
}

/* Where `s` already resolves in [pool, pool+len), or UINT32_MAX. */
static uint32_t find(uintptr_t pool, uint32_t from, uint32_t len, const char *s, uint32_t n) {
    for (uint32_t i = from; i + n < len; ++i) {
        uint32_t k = 0;
        while (k < n && peek8(pool + i + k) == (uint8_t)s[k]) ++k;
        if (k == n && peek8(pool + i + n) == 0) return i;
    }
    return UINT32_MAX;
}

static void copy(uintptr_t to, uintptr_t from, uint32_t n) {
    uint32_t i = 0;
    if (!((to | from) & 3u))
        for (; i + 4 <= n; i += 4) poke(to + i, peek(from + i));
    for (; i < n; ++i) poke8(to + i, peek8(from + i));
}

/* A new copy of the current pool with `capacity` bytes, switched in.
 * b_step/b_gate: the stepper's words (Experiment B); 0 disables its gates. */
static uintptr_t grow(uintptr_t reader, uintptr_t pool, uint32_t len, uint32_t capacity,
                      uintptr_t b_step, uintptr_t b_gate) {
    uintptr_t block, fresh;
    UIS_B_AT(b_step, b_gate, 0x302u, 2u);       /* the alloc (Experiment A proved it) */
    block = alloc(UIS_HEADER + capacity);
    if (!block || (block & 7u)) return 0;
    fresh = block + UIS_HEADER;
    UIS_B_AT(b_step, b_gate, 0x304u, 3u);       /* the copy: private dst, stock src */
    copy(fresh, pool, len);                     /* the room past it is written
                                                   string by string */
    UIS_B_AT(b_step, b_gate, 0x306u, 4u);       /* private header + the proven publish */
    poke(block + 0, UIS_MAGIC);
    poke(block + 4, UIS_VERSION);
    poke(block + 8, capacity);
    poke(block + 12, 0);
    publish();
    /* pointer first: every offset below the old length means the same in
     * both, so a resolve between the two stores is still right */
    UIS_B_AT(b_step, b_gate, 0x308u, 5u);       /* THE SWITCH: the first shared write */
    poke(reader + READER_POOL, fresh);
    publish();
    return fresh;
}

/* stock_at as uis_intern_hinted takes it; FULL = no hint, scan everything. */
#define FULL 0xFFFFFFFEu

static uint32_t intern(const char *s, uint32_t n, uint32_t stock_at, uint32_t *offset) {
    uintptr_t reader = uis_reader(), pool;
    uint32_t len, at, end, capacity, from = 0;
    if (!s || !offset || !n || s[n] != 0) return UIS_INVALID;
    for (uint32_t i = 0; i < n; ++i) if (!s[i]) return UIS_INVALID;
    if (!reader) return UIS_NO_READER;
    if (!uis_pool_known(reader)) return UIS_UNKNOWN_POOL;
    pool = peek(reader + READER_POOL);
    len = peek(reader + READER_LEN);
    if (stock_at == UIS_NOT_STOCK) {
        from = UIS_STOCK_LEN;               /* the stock bytes never change */
    } else if (stock_at != FULL && stock_at < UIS_STOCK_LEN - n) {
        uint32_t k = 0;                     /* a builder's hint: check it */
        while (k < n && peek8(pool + stock_at + k) == (uint8_t)s[k]) ++k;
        if (k == n && peek8(pool + stock_at + n) == 0) { *offset = stock_at; return UIS_OK; }
    }
    if ((at = find(pool, from, len, s, n)) != UINT32_MAX) { *offset = at; return UIS_OK; }

    at = (len + 3u) & ~3u;                      /* new strings start aligned */
    end = at + n + 1u;
    capacity = pool == UIS_STOCK_POOL ? 0 : peek(pool - 8);
    if (end > capacity) {
        uint32_t want = end + UIS_HEADROOM;
        if (capacity && want < 2u * capacity) want = 2u * capacity;
        if (!(pool = grow(reader, pool, len, want, 0u, 0u))) return UIS_NO_MEMORY;
    }
    for (uint32_t i = len; i < at; ++i) poke8(pool + i, 0);
    for (uint32_t i = 0; i < n; ++i) poke8(pool + at + i, (uint8_t)s[i]);
    poke8(pool + at + n, 0);
    publish();
    poke(reader + READER_LEN, end);             /* the new string exists now */
    publish();
    *offset = at;
    return UIS_OK;
}

/* Many at once: each string found where uis_intern would find it, the missing
 * ones appended together and published with ONE pair of D-cache cleans instead
 * of a pair per string (Sensor Lab: ~40 strings; each clean pair is boot time).
 * s[i] are resident C strings, offsets[i] receives each.  Strings repeated in s
 * get one copy.  stock_at: 0, or UIS_NOT_STOCK when none of them is a stock
 * string (uis_intern_hinted's meaning).  On any failure nothing new is visible
 * (the length is not moved). */
uint32_t uis_intern_many(const char *const *s, uint32_t count, uint32_t stock_at, uint32_t *offsets
#ifdef FPL_EXPERIMENT_B
                         , uintptr_t b_step, uintptr_t b_gate
#endif
                         ) {
#ifndef FPL_EXPERIMENT_B
    uintptr_t b_step = 0, b_gate = 0;   /* the gates compile away */
#endif
    uintptr_t reader = uis_reader(), pool;
    uint32_t len, end, capacity, pending = 0;
    /* UIS_NOT_STOCK: the builder knows none is a stock string -- search only
     * what other sups appended (r13: the whole-pool scans were the boot time) */
    uint32_t from = stock_at == UIS_NOT_STOCK ? UIS_STOCK_LEN : 0;
    if (!s || !offsets) return UIS_INVALID;
    if (!reader) return UIS_NO_READER;
    if (uis_pool_known(reader) == 0) return UIS_UNKNOWN_POOL;
    pool = peek(reader + READER_POOL);
    len = peek(reader + READER_LEN);
    end = len;
    UIS_B_AT(b_step, b_gate, 0x301u, 1u);   /* reader + pool validated; the scan comes next */
    /* where each lands: found, or after what is pending (aligned, as uis_intern) */
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t n = 0, at;
        if (!s[i]) return UIS_INVALID;
        while (s[i][n]) ++n;
        if (!n) return UIS_INVALID;
        if ((at = find(pool, from, len, s[i], n)) != UINT32_MAX) { offsets[i] = at; continue; }
        for (uint32_t j = 0; j < i; ++j) {            /* the same text earlier in this batch */
            uint32_t k = 0;
            while (s[j][k] && s[j][k] == s[i][k]) ++k;
            if (!s[j][k] && !s[i][k]) { at = offsets[j]; break; }
        }
        if (at != UINT32_MAX) { offsets[i] = at; continue; }
        at = (end + 3u) & ~3u;
        offsets[i] = at;
        end = at + n + 1u;
        pending++;
    }
    if (!pending) return UIS_OK;
    capacity = pool == UIS_STOCK_POOL ? 0 : peek(pool - 8);
    if (capacity < end) {                      /* a bigger copy, as uis_intern grows */
        uint32_t want = end + UIS_HEADROOM;
        if (capacity && want < 2u * capacity) want = 2u * capacity;
        if (!(pool = grow(reader, pool, len, want, b_step, b_gate))) return UIS_NO_MEMORY;
    }
    for (uint32_t i = len; i < end; ++i) poke8(pool + i, 0);
    for (uint32_t i = 0; i < count; ++i) {
        if (offsets[i] < len) continue;
        for (uint32_t k = 0; s[i][k]; ++k) poke8(pool + offsets[i] + k, (uint8_t)s[i][k]);
    }
    publish();
    UIS_B_AT(b_step, b_gate, 0x30au, 6u);      /* the second shared write */
    poke(reader + READER_LEN, end);             /* every new string exists now */
    publish();
    return UIS_OK;
}

uint32_t uis_intern(const char *s, uint32_t n, uint32_t *offset) {
    return intern(s, n, FULL, offset);
}

uint32_t uis_intern_hinted(const char *s, uint32_t n, uint32_t stock_at, uint32_t *offset) {
    return intern(s, n, stock_at, offset);
}


uint32_t uis_stock_has(uintptr_t s, uint32_t n, uint32_t stock_at) {
    uintptr_t reader = uis_reader(), pool;
    if (!s || !n || !reader || !uis_pool_known(reader) || stock_at >= UIS_STOCK_LEN ||
        n >= UIS_STOCK_LEN - stock_at)
        return 0;
    pool = peek(reader + READER_POOL);
    for (uint32_t k = 0; k < n; ++k) if (peek8(pool + stock_at + k) != peek8(s + k)) return 0;
    return peek8(pool + stock_at + n) == 0;
}
