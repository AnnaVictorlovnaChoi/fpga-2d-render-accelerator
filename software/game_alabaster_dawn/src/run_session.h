#ifndef AD_RUN_SESSION_H
#define AD_RUN_SESSION_H
#include <stdint.h>
/* No heap, MMIO or shared IRQ state. The caller supplies debounced edges. */
typedef struct {
    uint32_t released_ms;
    uint8_t terminal, releasing, armed, requested;
} RunRestart;
static inline void run_restart_input(RunRestart *s, uint32_t now,
                                    int gameover, uint8_t raw, uint8_t edges) {
    /* Release qualification survives the HOLD -> GAMEOVER render. A key
     * already released must not require another release after a blocking CG
     * draw. A press outside GAMEOVER disarms it, so held ending ESC is inert. */
    s->terminal=(uint8_t)!!gameover;
    if(!gameover)s->requested=0;
    if (!(raw&1u)) {
        s->releasing=0;
        if(!gameover)s->armed=0;
        else if(s->armed&&(edges&1u)) {s->requested=1;s->armed=0;}
    } else {
        if(!s->releasing){s->released_ms=now;s->releasing=1;}
        if((uint32_t)(now-s->released_ms)>=8u&&!s->requested)s->armed=1;
    }
}
/* Nonoverlapping completed one-second windows on the raw CLINT clock.
 * Count successful, GPU-complete submissions and observed consumed-seq deltas;
 * never VSYNC ticks. Delayed polls normalize by actual elapsed ticks. */
typedef struct {
    uint64_t start;
    uint32_t submitted, consumed, last_seq;
    uint32_t processing_fps, display_fps, revision;
    uint8_t enabled;
} RunRates;
static inline void run_rates_reset(RunRates *r,uint64_t now,uint32_t seq){
    *r=(RunRates){.start=now,.last_seq=seq,.enabled=1};
}
static inline int run_rates_poll(RunRates *r,uint64_t now,uint64_t hz,uint32_t seq){
    if(!r->enabled)return 0;
    r->consumed+=(uint32_t)(seq-r->last_seq);r->last_seq=seq;
    uint64_t span=now-r->start;
    if(span<hz)return 0;
    r->processing_fps=(uint32_t)((uint64_t)r->submitted*hz/span);
    r->display_fps=(uint32_t)((uint64_t)r->consumed*hz/span);
    r->submitted=r->consumed=0;r->start=now;++r->revision;return 1;
}
static inline void run_rates_submit(RunRates *r){if(r->enabled)++r->submitted;}
#endif
