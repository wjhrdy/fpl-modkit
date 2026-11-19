/* Host fixture for screenflip_fpl/native/profile.c: fake MMIO and getters. */
#include <stdint.h>
#include "profile.h"

/* The fp addresses are only host-fixture keys here; they are not fp L bindings. */
#define LAYER      0x30190044u
#define TOUCH      0xC376082Cu
#define TOUCH_AT   0xC3760830u
#define EDITING    0xC376228Cu
#define STATUS     0xC3033830u
#define RECORDING  (STATUS + 0x9Cu)

static uint32_t cells[5];
static uint32_t display_value, cinema_value, style_value;
static uint32_t relayouts, stores, queue_count, store_result = 1;
static uint8_t stored[FPF_SLOTS];
static uint32_t queued[8];
static struct fplflip flip;

uint32_t fplflip_host_read(uintptr_t address) {
    switch ((uint32_t)address) {
    case LAYER: return cells[0];
    case TOUCH: return cells[1];
    case TOUCH_AT: return cells[2];
    case EDITING: return cells[3];
    case RECORDING: return cells[4];
    default: return 0;
    }
}
void fplflip_host_write(uintptr_t address, uint32_t value) {
    switch ((uint32_t)address) {
    case LAYER: cells[0] = value; break;
    case TOUCH: cells[1] = value; break;
    case TOUCH_AT: cells[2] = value; break;
    case EDITING: cells[3] = value; break;
    case RECORDING: cells[4] = value; break;
    default: break;
    }
}
uintptr_t fplflip_host_settings(void) { return 0xC31AB100u; }
uintptr_t fplflip_host_display(uintptr_t settings) { (void)settings; return display_value; }
uintptr_t fplflip_host_cinema(uintptr_t settings) { (void)settings; return cinema_value; }
uintptr_t fplflip_host_style(uintptr_t settings) { (void)settings; return style_value; }
void fplflip_host_relayout(uint32_t message) { (void)message; ++relayouts; }
uintptr_t fplflip_host_status(void) { return STATUS; }

/* The host engine path (FPLFLIP_HOST_TEST): fake buffers in the validated
 * range, two distinct addresses so the double-buffer pair forms; the
 * release and clean are counters (the fixture has no real memory). */
static uint32_t host_alloc_calls;
uintptr_t fplflip_host_alloc(uint32_t bytes) {
    (void)bytes;
    return 0x45000000u + 0x1000000u * (host_alloc_calls++ & 1u);
}
void fplflip_host_release(uint32_t *desc) { (void)desc; }
void fplflip_host_clean(void) {}

static uint32_t host_store(void *product, const uint8_t table[FPF_SLOTS]) {
    (void)product;
    ++stores;
    for (uint32_t n = 0; n < FPF_SLOTS; ++n) stored[n] = table[n];
    return store_result;
}
static void host_queue(void *product, uint32_t value) {
    (void)product;
    if (queue_count < 8) queued[queue_count++] = value;
}

void host_reset(void) {
    relayouts = stores = queue_count = 0;
    store_result = 1;
    display_value = cinema_value = style_value = 0;
    for (uint32_t n = 0; n < FPF_SLOTS; ++n) stored[n] = 0;
    for (uint32_t n = 0; n < 8; ++n) queued[n] = 0;
}

uint32_t host_init(void) {
    struct fplflip_live live;
    live.layer_register = LAYER;
    live.touch_flag = TOUCH;
    live.touch_vtable_at = TOUCH_AT;
    live.touch_vtable = 0x00C0CCE65Cu;
    live.editing_custom = EDITING;
    live.status_object = STATUS;
    live.recording_offset = 0x9Cu;
    return fplflip_init(&flip, &live, 0, host_store, host_queue);
}

void host_changed(uint32_t key, uint32_t value) {
    fpf_changed(&flip.state, key, value);
}
void host_poll(void) { fplflip_poll(&flip); }
void host_set(uint32_t layer, uint32_t touch, uint32_t touch_at, uint32_t editing,
              uint32_t recording) {
    cells[0] = layer; cells[1] = touch; cells[2] = touch_at;
    cells[3] = editing; cells[4] = recording;
}
void host_set_mode(uint32_t display, uint32_t cinema, uint32_t style) {
    display_value = display; cinema_value = cinema; style_value = style;
}
void host_set_store_result(uint32_t result) { store_result = result; }
uint32_t host_layer(void) { return cells[0]; }
uint32_t host_touch(void) { return cells[1]; }
uint32_t host_relayouts(void) { return relayouts; }
uint32_t host_stores(void) { return stores; }
uint32_t host_stored(uint32_t n) { return n < FPF_SLOTS ? stored[n] : 0; }
uint32_t host_queued(uint32_t n) { return n < 8 ? queued[n] : 0; }
uint32_t host_state(uint32_t field) {
    switch (field) {
    case 0: return flip.state.polls;
    case 1: return flip.state.switches;
    case 2: return flip.state.applies;
    case 3: return flip.state.refused;
    case 4: return flip.state.pushes;
    case 5: return flip.state.edits;
    case 6: return flip.state.saves;
    case 7: return flip.state.save_failed;
    case 8: return flip.state.last_apply;
    case 9: return flip.engine.want;
    default: return 0;
    }
}
