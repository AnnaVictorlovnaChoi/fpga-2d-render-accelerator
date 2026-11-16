#ifndef AD_CPU_PROBE_H
#define AD_CPU_PROBE_H
#include <stdint.h>
#ifndef AD_CPU_PROBE
#define AD_CPU_PROBE 0
#endif
typedef enum { CP_BOOK, CP_UART, CP_PIPE, CP_INPUT, CP_MANAGE, CP_DIAG, CP_RENDER, CP_GATE, CP_SNAPSHOT, CP_CANDIDATE, CP_READY, CP_READY_BUSY, CP_CONSUME, CP_CONSUME_BUSY, CP_PHASES } CpuProbePhase;
typedef enum { CP_LOOPS, CP_CHECK, CP_READY_FALSE, CP_PENDING_FALSE, CP_SAME_MS, CP_GATE_SKIP, CP_NEW_CANDIDATE, CP_DRAW, CP_CONTINUE, CP_EVENTS } CpuProbeEvent;
typedef struct { uint64_t ticks,wait,max; uint32_t calls,exits,continues; } CpuProbeStat;
typedef struct { CpuProbeStat phase[CP_PHASES]; uint64_t wall,residual,wait_residual,reads; uint32_t event[CP_EVENTS],errors; } CpuProbeReport;
#if AD_CPU_PROBE
uint64_t cpu_probe_clock(void);
uint64_t cpu_probe_gpu(void);
void cpu_probe_start(void);
void cpu_probe_phase(CpuProbePhase phase);
void cpu_probe_event(CpuProbeEvent event);
void cpu_probe_relabel(CpuProbePhase phase);
#define CP_LABEL(p) cpu_probe_relabel(p)
void cpu_probe_continue(void);
void cpu_probe_take(CpuProbeReport *report);
#define CP_PHASE(p) cpu_probe_phase(p)
#define CP_EVENT(e) cpu_probe_event(e)
#else
#define cpu_probe_start() ((void)0)
#define CP_LABEL(p) ((void)0)
#define CP_PHASE(p) ((void)0)
#define CP_EVENT(e) ((void)0)
#endif
/* Must expand to the enclosing loop's continue, never a do/while wrapper. */
#if AD_CPU_PROBE
#define CP_CLOSE_CONTINUE() cpu_probe_continue()
#else
#define CP_CLOSE_CONTINUE() ((void)0)
#endif
#endif
