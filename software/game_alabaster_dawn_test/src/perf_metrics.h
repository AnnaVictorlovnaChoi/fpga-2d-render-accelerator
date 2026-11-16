#ifndef G24_PERF_METRICS_H
#define G24_PERF_METRICS_H
#include <stdint.h>
/* Success-only completed-render throughput, tenths FPS. Not GPU busy ratio. */
static inline uint32_t perf_rate_x10(uint32_t frames,uint64_t elapsed_ticks,uint32_t hz){
 if(!elapsed_ticks||!frames)return 0;
 uint64_t v=(uint64_t)frames*hz*10u/elapsed_ticks;
 return v>99999u?99999u:(uint32_t)v;
}
/* DISP~ estimates successful processed-frame submissions per wall-time window,
 * not distinct pixels or a physical display ACK. A one-slot pipeline can burst
 * by one frame at window boundaries; do not hide that with an artificial cap. */
static inline uint32_t perf_display_estimate_x10(uint32_t raw_rate){return raw_rate;}
#endif
