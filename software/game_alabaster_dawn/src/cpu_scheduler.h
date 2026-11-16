#ifndef AD_CPU_SCHEDULER_H
#define AD_CPU_SCHEDULER_H
#include <stdint.h>
/* Enable only after the target core's non-NOP WFI has been verified. */
#ifndef AD_SCHEDULER_WFI
#define AD_SCHEDULER_WFI 0
#endif
#if AD_SCHEDULER_WFI && !defined(AD_SCHEDULER_TEST) && !defined(AD_SCHEDULER_VERIFIED_WFI_TARGET)
#error "This v10 target has no verified sleeping WFI; do not enable idle accounting"
#endif
/* RAM-only pending check; independent of WFI support. */
extern volatile uint32_t cpu_scheduler_due;
static inline int cpu_scheduler_is_due(void){return cpu_scheduler_due!=0;}
/* Return 1 iff armed. Return 0 means process now (due/IRQ/other timer owner). */
int cpu_scheduler_arm_deadline(uint64_t deadline);
void cpu_scheduler_cancel(void);
/* Absolute raw CLINT deadlines. No GPU ownership or game clocks here. */
uint64_t cpu_scheduler_earliest(uint64_t a, uint64_t b);
uint64_t cpu_scheduler_sleep_until(uint64_t deadline);
uint64_t cpu_scheduler_sleep_ticks(void);
uint32_t cpu_scheduler_sleep_calls(void);
/* Cumulative actual timer ISR entries, modulo uint32; no clock/GPIO reads. */
uint32_t cpu_scheduler_timer_irqs(void);
void cpu_scheduler_timer_isr(void);
#endif
