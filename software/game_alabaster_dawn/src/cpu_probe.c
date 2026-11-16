#include "cpu_probe.h"
#include <string.h>
#if AD_CPU_PROBE
static CpuProbeReport accum;
static uint64_t origin,mark,gmark,gorigin,reader_fee;
static CpuProbePhase active;
static int started;
static uint64_t stamp(void){++accum.reads;return cpu_probe_clock();}
static void close_phase(void){
 uint64_t end=stamp(),g=cpu_probe_gpu();
 CpuProbeStat *s=&accum.phase[active];
 uint64_t dt=end>=mark?end-mark:0,w=g>=gmark?g-gmark:0;
 if(end<mark||g<gmark||w>dt){++accum.errors;w=0;}
 /* Calibrated reader fee belongs to residual, never subtract actual GPU wait. */
 if(dt>=reader_fee&&dt-reader_fee>=w)dt-=reader_fee;
 s->ticks+=dt;s->wait+=w;if(dt>s->max)s->max=dt;++s->exits;
}
static void open_phase(CpuProbePhase p){active=p;gmark=cpu_probe_gpu();mark=stamp();++accum.phase[p].calls;}
void cpu_probe_start(void){uint64_t a=cpu_probe_clock(),b=cpu_probe_clock();reader_fee=b>=a?b-a:0;memset(&accum,0,sizeof accum);gorigin=cpu_probe_gpu();origin=stamp();started=1;open_phase(CP_BOOK);}
void cpu_probe_phase(CpuProbePhase p){if(!started)return;close_phase();open_phase(p);}
void cpu_probe_relabel(CpuProbePhase p){if(!started)return;--accum.phase[active].calls;active=p;++accum.phase[p].calls;}
void cpu_probe_event(CpuProbeEvent e){if(started)++accum.event[e];}
void cpu_probe_continue(void){if(!started)return;++accum.event[CP_CONTINUE];++accum.phase[active].continues;cpu_probe_phase(CP_BOOK);}
void cpu_probe_take(CpuProbeReport *out){
 uint64_t end,g,sum=0,wait=0;CpuProbePhase saved=active;
 if(!started){memset(out,0,sizeof *out);return;}
 close_phase();end=stamp();g=cpu_probe_gpu();
 accum.wall=end>=origin?end-origin:0;
 for(unsigned i=0;i<CP_PHASES;i++){sum+=accum.phase[i].ticks;wait+=accum.phase[i].wait;}
 if(sum>accum.wall||g<gorigin||g-gorigin<wait)++accum.errors;
 accum.residual=sum<=accum.wall?accum.wall-sum:0;
 accum.wait_residual=g>=gorigin&&g-gorigin>=wait?g-gorigin-wait:0;
 if(end<origin||g<gorigin||(g>=gorigin&&g-gorigin>accum.wall)||accum.wait_residual>accum.residual)++accum.errors;
 *out=accum;memset(&accum,0,sizeof accum);
 /* Cut at end, not after copy/reset: reporting overhead stays in next diag. */
 origin=mark=end;gorigin=gmark=g;active=saved;accum.phase[saved].calls=1;
}
#endif
