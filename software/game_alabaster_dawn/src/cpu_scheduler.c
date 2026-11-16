#include "cpu_scheduler.h"
#ifdef AD_SCHEDULER_TEST
#include "scheduler_test_platform.h"
#else
#include "v8_platform.h"
#endif
#ifndef AD_SCHEDULER_FENCE
#define AD_SCHEDULER_FENCE() __asm__ volatile("fence iorw,iorw" ::: "memory")
#define AD_SCHEDULER_WAIT() __asm__ volatile("wfi" ::: "memory")
#endif
static uint64_t sleep_ticks;
static uint32_t sleep_calls;
uint64_t cpu_scheduler_earliest(uint64_t a,uint64_t b){return a<b?a:b;}
uint64_t cpu_scheduler_sleep_ticks(void){return sleep_ticks;}
uint32_t cpu_scheduler_sleep_calls(void){return sleep_calls;}
volatile uint32_t cpu_scheduler_due=1;
static volatile uint32_t timer_owned;
static volatile uint32_t timer_irq_count;
uint32_t cpu_scheduler_timer_irqs(void){return timer_irq_count;}
void cpu_scheduler_timer_isr(void){
    ++timer_irq_count;
    if(timer_owned){csr_clear(mie,MIE_MTIE);timer_owned=0;cpu_scheduler_due=1;}
}
void cpu_scheduler_cancel(void){
#if defined(__riscv) || defined(AD_SCHEDULER_TEST)
    uint32_t status=csr_read(mstatus);
    csr_clear(mstatus,MSTATUS_MIE);
    if(timer_owned){csr_clear(mie,MIE_MTIE);timer_owned=0;}
    cpu_scheduler_due=1;
    if(status&MSTATUS_MIE)csr_set(mstatus,MSTATUS_MIE);
#else
    cpu_scheduler_due=1;
#endif
}
int cpu_scheduler_arm_deadline(uint64_t deadline){
#if defined(__riscv) || defined(AD_SCHEDULER_TEST)
    uint32_t status=csr_read(mstatus),enabled;
    int armed=0;
    csr_clear(mstatus,MSTATUS_MIE);
    enabled=csr_read(mie);
    cpu_scheduler_due=1;
    if(timer_owned){csr_clear(mie,MIE_MTIE);timer_owned=0;enabled&=~MIE_MTIE;}
    /* Never steal another owner's timer or wait with global IRQ disabled. */
    if(!(status&MSTATUS_MIE)||(enabled&MIE_MTIE))goto arm_done;
    if(deadline<=clint_getTime(SYSTEM_CLINT_CTRL)||(csr_read(mip)&enabled))goto arm_done;
    clint_setCmp(SYSTEM_CLINT_CTRL,deadline,0);
    AD_SCHEDULER_FENCE();
    /* Commit flag and ownership while ISR cannot run. A timer reaching its
     * deadline at any later point remains pending and is handled on restore. */
    cpu_scheduler_due=0;timer_owned=1;
    csr_set(mie,MIE_MTIE);
    if((csr_read(mip)&(enabled|MIE_MTIE))||deadline<=clint_getTime(SYSTEM_CLINT_CTRL)){
        csr_clear(mie,MIE_MTIE);timer_owned=0;cpu_scheduler_due=1;
    }else armed=1;
 arm_done:
    if(status&MSTATUS_MIE)csr_set(mstatus,MSTATUS_MIE);
    return armed;
#else
    (void)deadline;cpu_scheduler_due=1;return 0;
#endif
}
uint64_t cpu_scheduler_sleep_until(uint64_t deadline){
#if (defined(__riscv) || defined(AD_SCHEDULER_TEST)) && AD_SCHEDULER_WFI
    uint32_t status=csr_read(mstatus),enabled=csr_read(mie);
    uint64_t start,end,span=0;
    /* Never steal a timer owned by a caller or enable globally disabled IRQs. */
    if(!(status&MSTATUS_MIE)||(enabled&MIE_MTIE))return 0;
    csr_clear(mstatus,MSTATUS_MIE);
    start=clint_getTime(SYSTEM_CLINT_CTRL);
    if(deadline<=start || (csr_read(mip)&enabled))goto done;
    /* Driver writes compare high=FFFFFFFF, low, final high on RV32. */
    clint_setCmp(SYSTEM_CLINT_CTRL,deadline,0);
    AD_SCHEDULER_FENCE();
    csr_set(mie,MIE_MTIE);
    /* With global MIE clear, an enabled pending IRQ wakes WFI but cannot
     * race its ISR between the final test and WFI. Spurious wake is allowed. */
    if(!(csr_read(mip)&(enabled|MIE_MTIE))){
        start=clint_getTime(SYSTEM_CLINT_CTRL);
        if(start<deadline){
            AD_SCHEDULER_WAIT();
            end=clint_getTime(SYSTEM_CLINT_CTRL);
            if(end>=start){span=end-start;sleep_ticks+=span;++sleep_calls;}
        }
    }
    csr_clear(mie,MIE_MTIE);
 done:
    /* Only our timer bit changed; all other caller enables are untouched. */
    if(status&MSTATUS_MIE)csr_set(mstatus,MSTATUS_MIE);
    return span;
#else
    (void)deadline;return 0; /* Host/no-WFI never fabricates idle. */
#endif
}
