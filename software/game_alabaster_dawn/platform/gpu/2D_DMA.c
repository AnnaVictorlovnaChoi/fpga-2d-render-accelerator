#include "2D_DMA.h"
#include "io.h"
#include "gpio.h"
#include "bsp.h"
#include "userDef.h"
#include "plic.h"
#include "riscv.h"
#include "clint.h"

#define GPU_FRAME_PLIC_ID SYSTEM_PLIC_USER_INTERRUPT_A_INTERRUPT

#define GPU_STATUS_REG       (GPU_BASE + GPU_STATUS_OFFSET)
#define GPU_FIFO_STATUS_REG  (GPU_BASE + GPU_FIFO_STATUS_OFFSET)
#define GPU_CMD_REG          (GPU_BASE + GPU_CMD_OFFSET)
#define GPU_FRAME_RENDER_TIMEOUT 100000000u


static volatile int frame_new, frame_playing, frame_retiring;
static u8 pipeline_initialized;
static volatile u8 frame_irq_armed, switch_issued;
static volatile FramePipelineStats pipeline_stats;
static u64 last_frame_irq;
static u8 have_frame_irq;
/* Single foreground writer; ISR never records GPU waits. */
static uint64_t gpu_wait_total;
uint64_t gpu_wait_ticks_total(void) { return gpu_wait_total; }
void gpu_wait_record(uint64_t start, uint64_t end)
{
    uint64_t elapsed = end - start;
    if (elapsed < (UINT64_C(1) << 63)) gpu_wait_total += elapsed;
}
static u64 submit_time;
static volatile u8 submit_low_seen;
/* GPIO window is 100000 user clocks; PLIC pulse is only ~160 ns.
 * One millisecond is longer than that window at this 250 MHz ABI, and
 * much shorter than a 720p frame. This is duplicate-event rejection,
 * not a delay, GPU fence, or inferred count of missed video frames. */
#define FRAME_IRQ_DEDUP_TICKS (BSP_CLINT_HZ / 1000u)

static u32 pipeline_lock(void)
{
    u32 saved = csr_read(mstatus);
    csr_clear(mstatus, MSTATUS_MIE);
    return saved;
}
static void pipeline_unlock(u32 saved)
{
    if (saved & MSTATUS_MIE) csr_set(mstatus, MSTATUS_MIE);
}

__attribute__((weak)) void frame_pipeline_tick_isr(void)
{
}

extern void trap_entry(void);          // trap.S 提供
/* GPIO1 bit0帧切换中断：只锁存状态，不在中断中访问GPU或等待。 */
void frame_switch_isr(void)
{
    u64 now = clint_getTime(BSP_CLINT);
    if (!(gpio_getInput(GPIO1) & GPU_FRAME_SWITCH)) {
        frame_irq_armed = 1u;
        ++pipeline_stats.stale_irq;
        return; /* Delayed claim outside the selection window: do not switch. */
    }
    if (have_frame_irq && now - last_frame_irq < FRAME_IRQ_DEDUP_TICKS) {
        ++pipeline_stats.duplicate_irq;
        return;
    }
    if (!frame_irq_armed) ++pipeline_stats.rearmed_irq;
    frame_irq_armed = 0u;
    have_frame_irq = 1u;
    last_frame_irq = now;
    if (frame_new > 0 && (submit_low_seen ||
        now - submit_time >= FRAME_IRQ_DEDUP_TICKS)) {
        /* Request preceded this window, including recovery when foreground
         * CPU work missed the low interval. A full millisecond excludes
         * a delayed claim belonging to the submission's 400us window. */
        switch_issued = 1u;
    }
    ++pipeline_stats.software_ticks;
    frame_pipeline_tick_isr();
}

void frame_pipeline_poll(void)
{
    u32 saved = pipeline_lock();
    if (!(gpio_getInput(GPIO1) & GPU_FRAME_SWITCH)) {
        frame_irq_armed = 1u;
        if (frame_new > 0) {
            submit_low_seen = 1u;
            if (switch_issued) {
                /* 400us window ended, later than the 88.9us VSYNC sampling
                 * edge. This is not a physical framebuffer/drain ACK. */
                frame_retiring = frame_new = -1;
                switch_issued = 0u;
                ++pipeline_stats.consumed_seq;
            }
        }
    }
    pipeline_unlock(saved);
}
void isrRoutine(void)
{
    u32 claim;
//    bsp_printf("1\r\n");
    claim = plic_claim(BSP_PLIC, BSP_PLIC_CPU_0);
//    bsp_printf("PLIC claim=%u\r\n", claim);
    while (claim != 0u) {
        switch (claim) {
        case GPU_FRAME_PLIC_ID:        // SYSTEM_PLIC_USER_INTERRUPT_A_INTERRUPT
            frame_switch_isr();
            break;
        default:
            break;
        }
        plic_release(BSP_PLIC, BSP_PLIC_CPU_0, claim);
        claim = plic_claim(BSP_PLIC, BSP_PLIC_CPU_0);
//        if (claim != 0u)
//            bsp_printf("PLIC next claim=%u\r\n", claim);
    }
}

/* CPU-only optional one-shot timer hook; no frame/GPU state changes. */
__attribute__((weak)) void cpu_scheduler_timer_isr(void) { csr_clear(mie, MIE_MTIE); }
void trap(void)
{
//	bsp_printf("2\r\n");
    u32 mcause = csr_read(mcause);
    int is_interrupt = (mcause >> 31) & 1u;
    u32 cause = mcause & 0xFu;
//    bsp_printf("mcause=%08x cause=%u\r\n", mcause, cause);

    if (!is_interrupt) {                       // 异常一律返回
        return;
    }

    switch (cause) {
    case CAUSE_MACHINE_TIMER:
        cpu_scheduler_timer_isr();
        break;
    case CAUSE_MACHINE_EXTERNAL:
        isrRoutine();
        break;
    default:
        break;
    }
}


static u32 frame_base(u8 frame)
{
    switch (frame) {
    case 1u: return FRAMEBUFFER_BASE_1;
    case 2u: return FRAMEBUFFER_BASE_2;
    case 3u: return FRAMEBUFFER_BASE_3;
    default: return 0u;
    }
}


int frame_pipeline_init(u32 timeout)
{
    (void)timeout;
    csr_clear(mstatus, MSTATUS_MIE);
    frame_new = frame_retiring = -1;
    frame_playing = 1;
    frame_irq_armed = 1u;
    switch_issued = have_frame_irq = 0u;
    last_frame_irq = 0;
    pipeline_stats = (FramePipelineStats){0};

    pipeline_initialized = 1u;

    gpio_setOutputEnable(GPIO1, 0x3u);
    gpio_setOutput(GPIO1, 0x1u);//直接默认从第一帧开始显示，第一帧就先不渲染直接黑屏显示一帧，这样便于初始化
    csr_write(mtvec, trap_entry);
    csr_set(mie, MIE_MEIE);
    plic_set_threshold(BSP_PLIC, BSP_PLIC_CPU_0, 0u);
    plic_set_priority(BSP_PLIC, GPU_FRAME_PLIC_ID, 1u);
    plic_set_enable(BSP_PLIC, BSP_PLIC_CPU_0, GPU_FRAME_PLIC_ID, 1u);
    csr_set(mstatus, MSTATUS_MIE);
    /*
     * 启动约定：软件先把帧选择输出设为1，HDMI先显示帧1。
     * 此时帧切换输入尚未必产生有效脉冲，不能用 gpio_getInput()
     * 判断初始化是否成功；后续由真实 PLIC 事件和 GPIO 低窗口推进消费。
     */
    return 0;
}

int frame_pipeline_acquire(u32 timeout)
{
    u32 saved = pipeline_lock();
    int result = -1;
    (void)timeout;
    if (!pipeline_initialized) result = -2;
    else for (int frame = 1; frame <= 3; ++frame) {
        if (frame != frame_new && frame != frame_playing && frame != frame_retiring) {
            result = frame;
            break;
        }
    }
    pipeline_unlock(saved);
    return result;
}

int frame_pipeline_submit(u8 frame)
{
    u32 saved = pipeline_lock();
    int result = 0;
    if (!pipeline_initialized || frame < 1u || frame > 3u) result = -1;
    else if (frame_new > 0) {
        ++pipeline_stats.busy_pending;
        result = -3;
    } else if (frame == frame_playing || frame == frame_retiring) result = -2;
    else {
        u32 status = read_u32(GPU_STATUS_REG);
        if (status & GPU_STATUS_ERROR) {
            ++pipeline_stats.gpu_error;
            result = -4;
        } else if ((status & (GPU_STATUS_DONE | GPU_STATUS_BUSY | GPU_STATUS_EMPTY)) !=
                   (GPU_STATUS_DONE | GPU_STATUS_EMPTY)) result = -5;
        else {
            /* Publish only after a real GPU completion. ISR never emits GPU
             * commands or waits. Keep both old/new destinations protected. */
            submit_time = clint_getTime(BSP_CLINT);
            submit_low_seen = !(gpio_getInput(GPIO1) & GPU_FRAME_SWITCH);
            frame_retiring = frame_playing;
            frame_new = frame;
            frame_playing = frame;
            switch_issued = 0u;
            gpio_setOutput(GPIO1, frame);
        }
    }
    if (result) ++pipeline_stats.rejected_submit;
    pipeline_unlock(saved);
    return result;
}

u32 frame_pipeline_consumed_seq(void)
{
    return pipeline_stats.consumed_seq;
}

void frame_pipeline_get_stats(FramePipelineStats *out)
{
    u32 saved;
    if (!out) return;
    saved = pipeline_lock();
    *out = pipeline_stats;
    out->selected_request = (u32)frame_playing;
    out->pending = frame_new > 0 ? (u32)frame_new : 0u;
    out->retiring = frame_retiring > 0 ? (u32)frame_retiring : 0u;
    out->switch_issued = switch_issued;
    pipeline_unlock(saved);
}

static int gpu_send_word(u32 value)
{
    write_u32(value, GPU_CMD_REG);
    return 0;
}


int gpu_wait_fifo_ready(void)
{
    uint64_t begin = clint_getTime(BSP_CLINT), end;
    int rc;
    for (;;) {
        frame_pipeline_poll();
        if (read_u32(GPU_STATUS_REG) & GPU_STATUS_ERROR) {
            ++pipeline_stats.gpu_error;
            rc = -3;
            break;
        }
        if (!(read_u32(GPU_FIFO_STATUS_REG) &
              (GPU_STATUS_FULL | GPU_STATUS_PROG_FULL))) {
            rc = 0;
            break;
        }
        if (clint_getTime(BSP_CLINT) - begin >= (uint64_t)BSP_CLINT_HZ * 2u) {
            rc = -1;
            break;
        }
    }
    end = clint_getTime(BSP_CLINT);
    gpu_wait_record(begin, end);
    return rc;
}

int gpu_prepare_copy(GpuPreparedCopy *out, u32 source, u32 x, u32 y,
                     u32 width, u32 height, u32 frame)
{
    u32 offset;
    if (!out) return -1;
    out->frame = 0; /* Failed preparation cannot emit a stale packet. */
    if (frame < 1 || frame > 3 || !width || !height ||
        x >= FRAME_WIDTH || y >= FRAME_HEIGHT ||
        width > FRAME_WIDTH-x || height > FRAME_HEIGHT-y || (source & 1u) ||
        (u64)source + FRAME_STRIDE_BYTES * FRAME_HEIGHT > 0x100000000ull)
        return -1;
    offset = y * FRAME_STRIDE_BYTES + x * 2u;
    out->src = source + offset;
    out->dest = frame_base((u8)frame) + offset;
    out->dims = (height << 16) | width;
    out->frame = frame;
    return 0;
}
int gpu_enqueue_copy(const GpuPreparedCopy *p)
{
    u32 saved;
    int rc = 0;
    if (!p || p->frame < 1 || p->frame > 3) return -1;
    /* Prepared value is private to foreground. Lock the ownership check and
     * packet together; never hold the lock across FIFO waiting. */
    saved = pipeline_lock();
    if (pipeline_initialized && (p->frame == (u32)frame_playing ||
        p->frame == (u32)frame_new || p->frame == (u32)frame_retiring)) rc = -8;
    else if (read_u32(GPU_FIFO_STATUS_REG) &
             (GPU_STATUS_FULL | GPU_STATUS_PROG_FULL)) rc = -2;
    else {
        gpu_send_word(1u); gpu_send_word(p->src); gpu_send_word(p->dest);
        gpu_send_word(p->dims); gpu_send_word(FRAME_STRIDE_BYTES);
        gpu_send_word(FRAME_STRIDE_BYTES); gpu_send_word(0u);
    }
    pipeline_unlock(saved);
    return rc;
}

int bitblt_wait(u32 timeout)
{
    uint64_t begin = clint_getTime(BSP_CLINT);
    while (timeout != 0u) {
        frame_pipeline_poll();
        u32 status = read_u32(GPU_STATUS_REG);
        if (status & GPU_STATUS_ERROR) {
            ++pipeline_stats.gpu_error;
            gpu_wait_record(begin, clint_getTime(BSP_CLINT));
            return -2;
        }
        if ((status & (GPU_STATUS_DONE | GPU_STATUS_BUSY | GPU_STATUS_EMPTY)) ==
            (GPU_STATUS_DONE | GPU_STATUS_EMPTY)) {
            gpu_wait_record(begin, clint_getTime(BSP_CLINT));
            return 0;
        }
        --timeout;
    }
    gpu_wait_record(begin, clint_getTime(BSP_CLINT));
    bsp_printf("GPU wait timeout: status=%08x fifo=%08x\r\n",
               read_u32(GPU_STATUS_REG), read_u32(GPU_FIFO_STATUS_REG));
    return -1;
}



int bitblt(u32 ctrl, u32 src, u32 dest, u32 width, u32 height,
           u32 src_stride, u32 dest_stride, u32 color)
{
    int result;
    u32 fifo_status;
//    u32 timeout = 100000000u;
//    while (read_u32(GPU_FIFO_STATUS_REG) &
//           (GPU_STATUS_FULL | GPU_STATUS_PROG_FULL)) {
//        if (timeout-- == 0u) {
//            bsp_printf("GPU command FIFO timeout: status=%08x fifo=%08x\r\n",
//                       read_u32(GPU_STATUS_REG), read_u32(GPU_FIFO_STATUS_REG));
//            return -1;
//        }
//    }
//    if ((ctrl & 0x7u) != GPU_OPERATION_COPY &&
//        (ctrl & 0x7u) != GPU_OPERATION_COLOR &&
//        (ctrl & 0x7u) != GPU_OPERATION_SPRITE &&
//        (ctrl & 0x7u) != GPU_OPERATION_ALPHA &&
//        (ctrl & 0x7u) != GPU_OPERATION_ZOOM &&
//        (ctrl & 0x7u) != GPU_OPERATION_ALPHASPRITE)
//        return -1;
//    if (width == 0u || width > 0xffffu || height == 0u || height > 0xffffu)
//        return -3;
//    if (((ctrl & 0x7u) == GPU_OPERATION_COPY ||
//         (ctrl & 0x7u) == GPU_OPERATION_SPRITE ||
//         (ctrl & 0x7u) == GPU_OPERATION_ALPHA ||
//         (ctrl & 0x7u) == GPU_OPERATION_ALPHASPRITE) &&
//        (src_stride < width * 2u || src_stride > 0xffffu))
//        return -4;
//    if (dest_stride < width * 2u || dest_stride > 0xffffu)
//        return -5;
//    if ((src & 1u) || (dest & 1u) || (src_stride & 1u) || (dest_stride & 1u))
//        return -6;

    /* Reject malformed spans before any FIFO word is emitted. Source address
     * ownership remains with the resource allocator (sprites use packed data). */
    if (ctrl > GPU_OPERATION_ALPHASPRITE || !width || width > 0xffffu ||
        !height || height > 0xffffu || dest_stride < width * 2u ||
        dest_stride > 0xffffu || (dest & 1u) || (dest_stride & 1u)) return -6;
    {
        u64 end = (u64)dest + (u64)(height - 1u) * dest_stride + width * 2u;
        if (end > 0x100000000ull) return -6;
        for (u8 frame = 1; frame <= 3; ++frame) {
            u32 base = frame_base(frame);
            u32 limit = base + FRAME_STRIDE_BYTES * FRAME_HEIGHT;
            if ((u64)dest < (u64)base + 0x200000u && end > base) {
                if (dest < base || end > limit || dest_stride != FRAME_STRIDE_BYTES ||
                    ((dest - base) % FRAME_STRIDE_BYTES) + width * 2u > FRAME_STRIDE_BYTES)
                    return -7;
                if (pipeline_initialized && (frame == frame_playing ||
                    frame == frame_new || frame == frame_retiring)) return -8;
            }
        }
    }
    fifo_status = read_u32(GPU_FIFO_STATUS_REG);
    if (fifo_status & (GPU_STATUS_FULL | GPU_STATUS_PROG_FULL))
        return -2;

    result = gpu_send_word(((ctrl & 0x7u) << 8) | 1u);
    if (result != 0) return result;
    result = gpu_send_word(src);
    if (result != 0) return result;
    result = gpu_send_word(dest);
    if (result != 0) return result;
    result = gpu_send_word((height << 16) | width);
    if (result != 0) return result;
    result = gpu_send_word(src_stride);
    if (result != 0) return result;
    result = gpu_send_word(dest_stride);
    if (result != 0) return result;
    result = gpu_send_word(color);
    if (result != 0) return result;

    /* Count only a completely accepted alpha packet, including cache replay.
     * Strong implementation belongs to render_metrics.c in both log variants. */
    if (ctrl == GPU_OPERATION_ALPHA || ctrl == GPU_OPERATION_ALPHASPRITE) {
        extern void render_metrics_alpha_accepted(void);
        render_metrics_alpha_accepted();
    }
    return 0;
}

//函数参数分别是原图像地址，原图像大小，需要截取图像大小，图像截取起始点，图像搬运目的帧起始点，目的帧
int pic_blt(u32 base_addr, u32 src_width, u32 src_height,
            u32 width, u32 height, u32 start_pointx, u32 start_pointy,
            u32 dest_pointx, u32 dest_pointy, u8 frame,u8 cachein,u8 cachenum)
{
    u32 dest_base;
    u32 src;
    u32 dest;

//    if (src_width == 0u || src_height == 0u || width == 0u || height == 0u ||
//        start_pointx > src_width || width > src_width - start_pointx ||
//        start_pointy > src_height || height > src_height - start_pointy ||
//        dest_pointx > FRAME_WIDTH || width > FRAME_WIDTH - dest_pointx ||
//        dest_pointy > FRAME_HEIGHT || height > FRAME_HEIGHT - dest_pointy ||
//        frame < 1u || frame > 3u)
//        return -1;
    u32 CacheOP = ((u32)(cachein & 1u) << 31) | ((u32)(cachenum & 7u) << 28);
    dest_base = frame_base(frame);
    src = base_addr + 2u * (start_pointy * src_width + start_pointx);
    dest = dest_base + dest_pointy * FRAME_STRIDE_BYTES + 2u * dest_pointx;

    return bitblt(GPU_OPERATION_COPY, src, dest, width, height,
                  src_width * 2u, FRAME_STRIDE_BYTES, CacheOP);
}

int pic_colorfill(u32 width, u32 height, u32 dest_pointx, u32 dest_pointy, u8 frame,u16 color){
//	if (width == 0u || height == 0u || dest_pointx > FRAME_WIDTH || width > FRAME_WIDTH - dest_pointx ||
//	        dest_pointy > FRAME_HEIGHT || height > FRAME_HEIGHT - dest_pointy || frame < 1u || frame > 3u)
//	        return -1;
	u32 dest_base;
	dest_base = frame_base(frame);
	u32 dest;
	dest = dest_base + dest_pointy * FRAME_STRIDE_BYTES + 2u * dest_pointx;
    return bitblt(GPU_OPERATION_COLOR, 0, dest, width, height,
			FRAME_STRIDE_BYTES, FRAME_STRIDE_BYTES, color);

}
int pic_sprite(u32 base_addr, u32 src_width, u32 src_height,
        u32 width, u32 height, u32 start_pointx, u32 start_pointy,
        u32 dest_pointx, u32 dest_pointy, u8 frame,u8 cachein ,u8 cachenum){
//
//    if (src_width == 0u || src_height == 0u || width == 0u || height == 0u ||
//        start_pointx > src_width || width > src_width - start_pointx ||
//        start_pointy > src_height || height > src_height - start_pointy ||
//        dest_pointx > FRAME_WIDTH || width > FRAME_WIDTH - dest_pointx ||
//        dest_pointy > FRAME_HEIGHT || height > FRAME_HEIGHT - dest_pointy ||
//        frame < 1u || frame > 3u)
//        return -1;

    u32 dest_base = frame_base(frame);
    u32 src = base_addr + 2u * (start_pointy * src_width + start_pointx);
    u32 dest = dest_base + dest_pointy * FRAME_STRIDE_BYTES + 2u * dest_pointx;
    u32 CacheOP = ((u32)(cachein & 1u) << 31) | ((u32)(cachenum & 7u) << 28);
    return bitblt(GPU_OPERATION_SPRITE, src, dest, width, height,
                  src_width * 2u, FRAME_STRIDE_BYTES, CacheOP);
}
int pic_alpha(u32 base_addr, u32 src_width, u32 src_height,
        u32 width, u32 height, u32 start_pointx, u32 start_pointy,
        u32 dest_pointx, u32 dest_pointy, u8 frame,float alpha,u8 cachein ,u8 cachenum){

//if (src_width == 0u || src_height == 0u || width == 0u || height == 0u ||
//    start_pointx > src_width || width > src_width - start_pointx ||
//    start_pointy > src_height || height > src_height - start_pointy ||
//    dest_pointx > FRAME_WIDTH || width > FRAME_WIDTH - dest_pointx ||
//    dest_pointy > FRAME_HEIGHT || height > FRAME_HEIGHT - dest_pointy ||
//    frame < 1u || frame > 3u)
//    return -1;

u32 dest_base = frame_base(frame);
u32 src = base_addr + 2u * (start_pointy * src_width + start_pointx);
u32 dest = dest_base + dest_pointy * FRAME_STRIDE_BYTES + 2u * dest_pointx;

u32 alphainput=((int)(alpha*32))%32|((u32)(cachein & 1u) << 31) | ((u32)(cachenum & 7u) << 28);

return bitblt(GPU_OPERATION_ALPHA, src, dest, width, height,
              src_width * 2u, FRAME_STRIDE_BYTES, alphainput);
}

int pic_zoom(u32 base_addr, u32 src_width, u32 src_height,
        u32 width, u32 height, u32 start_pointx, u32 start_pointy,
        u32 dest_pointx, u32 dest_pointy, u8 frame,float zoom){
	u32 dest_base;
	u32 src;
	u32 dest;
	u32 scale_q6;
	u32 dst_width;
	u32 dst_height;

	if (src_width == 0u || src_height == 0u || width == 0u || height == 0u ||
	    !(zoom > 0.0f) || zoom > 64.0f || frame < 1u || frame > 3u ||
	    start_pointx > src_width || width > src_width - start_pointx ||
	    start_pointy > src_height || height > src_height - start_pointy)
	    return -11;

	/* 硬件比例表示每个目标像素跨过的源像素数，因此取显示倍率的倒数。 */
	scale_q6 = (u32)(64.0f / zoom + 0.5f);
	if (scale_q6 == 0u || scale_q6 > 255u)
	    return -12;
	/* 先检查浮点结果，再转换为 u32，避免大尺寸转换溢出。 */
	if ((double)width * (double)zoom > 65535.0 ||
	    (double)height * (double)zoom > 65535.0)
	    return -13;
	dst_width = (u32)(width * zoom + 0.5f);
	dst_height = (u32)(height * zoom + 0.5f);
	if (dst_width == 0u || dst_width > 0xffffu ||
	    dst_height == 0u || dst_height > 0xffffu ||
	    dest_pointx > FRAME_WIDTH || dst_width > FRAME_WIDTH - dest_pointx ||
	    dest_pointy > FRAME_HEIGHT || dst_height > FRAME_HEIGHT - dest_pointy)
	    return -14;

	dest_base = frame_base(frame);
	src = base_addr + 2u * (start_pointy * src_width + start_pointx);
	dest = dest_base + dest_pointy * FRAME_STRIDE_BYTES + 2u * dest_pointx;

	u32 zoominput = scale_q6;

	return bitblt(GPU_OPERATION_ZOOM, src, dest, dst_width, dst_height,
	              src_width * 2u, FRAME_STRIDE_BYTES, zoominput);
	}

int pic_alphasprite(u32 base_addr, u32 src_width, u32 src_height,
        u32 width, u32 height, u32 start_pointx, u32 start_pointy,
        u32 dest_pointx, u32 dest_pointy, u8 frame,float alpha,u8 cachein ,u8 cachenum){
	u32 dest_base;
	u32 src;
	u32 dest;

	if (src_width == 0u || src_height == 0u || width == 0u || height == 0u ||
	    start_pointx > src_width || width > src_width - start_pointx ||
	    start_pointy > src_height || height > src_height - start_pointy ||
	    dest_pointx > FRAME_WIDTH || width > FRAME_WIDTH - dest_pointx ||
	    dest_pointy > FRAME_HEIGHT || height > FRAME_HEIGHT - dest_pointy ||
	    frame < 1u || frame > 3u || !(alpha >= 0.0f) || alpha > 1.0f)
	    return -1;

	dest_base = frame_base(frame);
	src = base_addr + 2u * (start_pointy * src_width + start_pointx);
	dest = dest_base + dest_pointy * FRAME_STRIDE_BYTES + 2u * dest_pointx;

	/* Q0.5 使用 0..31；alpha=1 映射到硬件可表示的最大不透明度。 */
	u32 alphainput = ((u32)(cachein & 1u) << 31) | ((u32)(cachenum & 7u) << 28)|((alpha >= 1.0f) ? 31u : (u32)(alpha * 32.0f));

	return bitblt(GPU_OPERATION_ALPHASPRITE, src, dest, width, height,
	              src_width * 2u, FRAME_STRIDE_BYTES, alphainput);
	}
