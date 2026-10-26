// Stock-runtime compatibility shims for functions N64Recomp's own built-in
// `ignored_funcs` list (N64Recomp/src/symbol_lists.cpp) expects some
// runtime to provide under a `_recomp` suffix, but that stock
// N64ModernRuntime doesn't implement (confirmed by grepping its whole
// source -- see STATUS.md round 23 for how this was found and diagnosed).
// CMakeLists.txt's NABOO_FORKED_RUNTIME check leaves this file out when
// building against a runtime fork that already has them.
//
// Each implementation is a reading of real libultra behavior (documented,
// or read directly from this ROM's own bytes at the relevant address).
// They run in the shipping game; the ones with lower confidence are
// flagged individually below.

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "recomp.h"

// --- COP0 Status register read -------------------------------------------
// __osGetSR() just does `mfc0 $v0, C0_SR` on real hardware. Nothing in this
// runtime emulates COP0 state (see naboo.us.rev0.toml's cop0
// instruction patches -- every other read/write of it in this ROM's own
// code is nopped for the same reason), so there's no real value to return.
// librecomp's own equivalent for the sibling function __osSetFpcCsr_recomp
// (lib/N64ModernRuntime/librecomp/src/ultra_translation.cpp) does the same
// thing for the same reason.
extern "C" void __osGetSR_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = 0;
}

// --- libultra thread scheduler internals ---------------------------------
// Real prototypes (confirmed against this ROM's own call sites -- e.g.
// __osEnqueueThread and __osDequeueThread are each called with the queue
// head in $a0 and the thread pointer in $a1, matching libultra's public
// prototype ordering): `__osEnqueueThread(OSThread **queue, OSThread *t)`,
// `__osDequeueThread(OSThread **queue, OSThread *t)`,
// `OSThread *__osPopThread(OSThread **queue)`, `void __osDispatchThread(void)`.
// ultramodern already implements the same priority-queue-of-OSThread
// operations these need (ultramodern/include/ultramodern/ultramodern.hpp)
// for its own osStartThread/osCreateThread/etc., operating directly on the
// guest OSThread queue linked list in rdram -- not ultramodern's own
// private state -- so calling straight through to those should be
// behaviorally equivalent regardless of which queue (a message queue's
// blocked-thread list, the run queue, etc.) is being manipulated.
extern "C" void __osEnqueueThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::thread_queue_insert(rdram, (PTR(PTR(OSThread)))(int32_t)ctx->r4, (PTR(OSThread))(int32_t)ctx->r5);
}

extern "C" void __osDequeueThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::thread_queue_remove(rdram, (PTR(PTR(OSThread)))(int32_t)ctx->r4, (PTR(OSThread))(int32_t)ctx->r5);
}

extern "C" void __osPopThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = ultramodern::thread_queue_pop(rdram, (PTR(PTR(OSThread)))(int32_t)ctx->r4);
}

// __osDispatchThread's real job (pick the next ready thread off the run
// queue and context-switch into it) is done very differently here than on
// real hardware: real hardware does it with a raw register/COP0 context
// restore, while ultramodern runs each N64 thread as a real host thread
// and switches via semaphore signaling. run_next_thread_and_wait is
// ultramodern's own equivalent entry point for "give up the CPU and let
// the scheduler pick who runs next."
extern "C" void __osDispatchThread_recomp(uint8_t* rdram, recomp_context*) {
    ultramodern::run_next_thread_and_wait(rdram);
}

// --- SI access serialization ----------------------------------------------
// __osSiCreateAccessQueue(void) creates a 1-slot OSMesgQueue used as a mutex
// around SI bus access, then posts one message so the "mutex" starts
// available. This ROM's own copy (0x40047A10) sets __osSiAccessQueueEnabled
// (0x4004ECB0) and calls osCreateMesgQueue(&0x40095638, &0x40095630, 1) and
// osSendMesg(&0x40095638, NULL, OS_MESG_NOBLOCK); replicated here so anything
// that later blocks on that queue finds the message.
extern "C" void __osSiCreateAccessQueue_recomp(uint8_t* rdram, recomp_context*) {
    constexpr int32_t si_access_queue_enabled = 0x4004ECB0;
    constexpr int32_t si_access_queue = 0x40095638;
    constexpr int32_t si_access_queue_msgs = 0x40095630;
    MEM_W(0, si_access_queue_enabled) = 1;
    osCreateMesgQueue(rdram, si_access_queue, si_access_queue_msgs, 1);
    osSendMesg(rdram, si_access_queue, 0, 0);
}

// --- Timer / VI internals --------------------------------------------------
// Both are called with no arguments from this ROM's VI manager thread code
// (vimgr_text_0188), matching their real void-void libultra prototypes.
// ultramodern manages both VI framebuffer swaps (via the renderer context,
// tied to the real osViSwapBuffer/osViGetCurrentFramebuffer exports) and
// timer expiry (ultramodern::init_timers, backing the real
// osSetTimer/osStopTimer exports) through its own separate mechanism
// already, so these ROM-internal calls -- which just update the N64-side
// bookkeeping structures those subsystems would otherwise touch on real
// hardware -- are made no-ops here rather than risk them fighting with
// ultramodern's own tracking of the same state. The game runs correctly
// with them as no-ops; if VI timing or timer-driven gameplay logic ever
// misbehaves, check whether the ROM reads that bookkeeping directly.
extern "C" void __osTimerInterrupt_recomp(uint8_t*, recomp_context*) {
}

extern "C" void __osViSwapContext_recomp(uint8_t*, recomp_context*) {
}

// --- RSP and RDP internals the game calls directly --------------------------
// func_40023558 starts a task by hand: osSpTaskLoad, then waits for the SP
// (__osSpDeviceBusy) and DMAs the task's ucode data to DMEM 0 again
// (__osSpRawStartDma) before osSpTaskStartGo. The runtime's task start
// (librecomp's rsp::run_task) copies the ucode data into DMEM itself, so
// the SP is never busy and the DMA is a no-op that succeeds.
extern "C" void __osSpDeviceBusy_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = 0;
}

extern "C" void __osSpRawStartDma_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = 0;
}

// osDpGetCounters(u32 counters[4]): the RDP's clock, command-busy, pipe-busy
// and TMEM-busy counters, read by func_4000B37C (performance numbers). There
// is no RDP here; report zeros.
extern "C" void osDpGetCounters_recomp(uint8_t* rdram, recomp_context* ctx) {
    gpr counters = ctx->r4;
    for (int i = 0; i < 4; i++) {
        MEM_W(i * 4, counters) = 0;
    }
}

// --- TLB ------------------------------------------------------------------
// func_40039960 (memory setup) can map the whole 0x41000000 code segment
// onto the top of RAM with osMapTLB and DMA it there, instead of paging it
// in. The runtime's address masking already sends 0x41000000 to the copy
// src/game/paged_segment.cpp loads at boot (the segment is read-only), so
// the mapping itself has nothing to do. STATUS.md round 5.
extern "C" void osMapTLB_recomp(uint8_t*, recomp_context*) {
}

// --- VI -------------------------------------------------------------------
// osViGetCurrentField: which field of an interlaced mode is being shown.
// The runtime presents whole frames; report field 0.
extern "C" void osViGetCurrentField_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = 0;
}
