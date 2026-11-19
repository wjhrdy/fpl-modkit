.syntax unified
.arm
.text
@ entry is at offset 0; not global, so every adr to it resolves here
/* ============================================================================
 * fpScreenFlip for fp L 3.02, VBIN boot-bin entry.
 *
 * The fp L stage2 calls an entry with ONE argument: r0 = the entry's own
 * resolved address (no service table, no path), in the loader's staging
 * buffer, which the loader hands back when the entry returns.  Everything
 * that has to outlive the call therefore gets its own allocation and is
 * copied there first -- the worker's spawn does exactly this (worker.S), and
 * so does this entry:
 *
 *   1. allocate one class-0 block of TOTAL bytes (0xC001D740/0xC001D7F0; the
 *      descriptor lives on the stack and is never freed, as in spawn);
 *      a refusal returns quietly, having written nothing.
 *   2. copy this whole blob (entry + C + the row's FPUI block + the state)
 *      into it, then D-cache clean + I-cache invalidate.
 *   3. continue INSIDE the copy, fill the state's first four words, and call
 *      fpl_card_init(state, copy, 0): it registers MV_fpScreenFlip, adds the
 *      row through uia_apply, subscribes, starts the task, and returns.
 *
 * The card declares the three fp L string sites this sup may touch as
 * journaled stock sections (stage2 writes them back at power-off); there is
 * no runtime claim table on this chain, so the entry claims nothing itself.
 * The panel register and the touch flag are MMIO/RAM, not firmware image,
 * and need no journal.
 *
 * Passed in by build_v3_flip_fpl.py: TOTAL, STATE_OFF, UI_OFF, UI_LEN and
 * the C entries' offsets from the blob start (OFF_INIT, OFF_TASK,
 * OFF_CHANGED; Thumb, bit 0 set).
 * ========================================================================== */

.equ F_GET,        0xC001D740
.equ F_ADDR,       0xC001D7F0
.equ F_DCACHE,     0xC000E91C
.equ F_ICACHE,     0xC000EABC

.macro LDA reg, value
    movw    \reg, #:lower16:\value
    movt    \reg, #:upper16:\value
.endm

entry:
    push    {r4, r5, r6, r7, r8, r9, r10, lr}   @ eight: 8-aligned
    adr     r5, entry                   @ r5 = the staging blob (entry at +0)

    /* 1. one allocation for the whole blob; the descriptor is never freed */
    sub     sp, sp, #16
    mov     r0, sp
    mov     r1, #0
    LDA     r2, TOTAL
    mov     r3, #0
    LDA     ip, F_GET
    blx     ip
    mov     r0, sp
    LDA     ip, F_ADDR
    blx     ip
    add     sp, sp, #16
    cmp     r0, #0
    beq     refused                     /* no memory: no flip, no row */

    /* 2. copy the blob into the allocation, word by word, forward */
    mov     r6, r0                      @ r6 = the copy base (kept intact)
    mov     r7, r5                      @ r7 = the walking source
    mov     r9, r0                      @ r9 = the walking destination
    LDA     r8, TOTAL
1:  ldr     r0, [r7], #4
    str     r0, [r9], #4
    subs    r8, r8, #4
    bne     1b
    LDA     ip, F_DCACHE
    blx     ip
    LDA     ip, F_ICACHE
    blx     ip

    /* 3. continue inside the copy */
    adr     r0, cont
    sub     r0, r0, r5                  @ cont's offset from the blob start
    add     r0, r0, r6                  @ + the copy base
    bx      r0

cont:
    /* Running in the copy now; re-locate everything against it. */
    adr     r5, entry                   @ r5 = the copy's blob base
    adr     r9, c_base                  @ r9 = the copy's C text base: the
                                        @ OFF_* offsets are text-relative
    /* the state's first four words, the shims' words */
    LDA     r4, STATE_OFF
    add     r4, r5, r4                  @ r4 = the state, in the copy
    LDA     r0, UI_OFF
    add     r0, r5, r0
    str     r0, [r4]                    @ ui_at
    LDA     r0, UI_LEN
    str     r0, [r4, #4]                @ ui_len
    adr     r0, task_shim
    str     r0, [r4, #8]                @ task_entry
    adr     r0, changed_shim
    str     r0, [r4, #12]               @ changed_entry
    str     r4, g_state
    LDA     r0, OFF_TASK
    add     r0, r9, r0
    str     r0, w_task
    LDA     r0, OFF_CHANGED
    add     r0, r9, r0
    str     r0, w_changed
    LDA     ip, F_DCACHE
    blx     ip

    mov     r0, r4
    mov     r1, r5                      @ the copy: the product's own home
    mov     r2, #0                      @ no path on this chain (self_path 0)
    LDA     ip, OFF_INIT
    add     ip, r9, ip
    blx     ip                          @ returns; its value is not interpreted
    pop     {r4, r5, r6, r7, r8, r9, r10, pc}

refused:
    pop     {r4, r5, r6, r7, r8, r9, r10, pc}

/* The task (tk_cre_tsk): fpl_task(state). */
task_shim:
    ldr     r0, g_state
    ldr     ip, w_task
    bx      ip

/* MV_fpScreenFlip written (UI thread): fpl_changed(descriptor, state). */
changed_shim:
    ldr     r1, g_state
    ldr     ip, w_changed
    bx      ip

    .align  2
g_state:    .word 0
w_task:     .word 0
w_changed:  .word 0
/* The flip value of each custom, as saved in this file (flip_fpl.c
 * FPF_SETTINGS_OFF): {"FFST", version 1, STILL Custom 1..4, CINE Custom
 * 1..4 (a byte each), check}.  v1 keeps the table per-boot: nothing
 * rewrites this record on the card. Built all 0 (normal). */
settings:
    .word 0x54534646, 1, 0, 0, 0x54534646 ^ 1 ^ 0 ^ 0 ^ 0xFFFFFFFF

    .align  3
c_base:
    /* the compiled C, then the FPUI block: build_v3_flip_fpl.py */
