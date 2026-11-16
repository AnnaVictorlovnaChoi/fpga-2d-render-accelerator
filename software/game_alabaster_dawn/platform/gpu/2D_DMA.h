#ifndef TWOD_DMA_H
#define TWOD_DMA_H

#include "type.h"
#include <stdint.h>

/* Foreground-only cumulative raw CLINT ticks; never reset. Record disjoint
 * blocking loops only, not async pending intervals or readiness probes.
 * Busywait includes frame_pipeline_poll inside the loop; it is NOT CPU idle.
 * Unsigned snapshot differences are valid for intervals shorter than 2^63 ticks.
 * No ISR writes, so RV32 foreground access needs no interrupt masking. */
uint64_t gpu_wait_ticks_total(void);
void gpu_wait_record(uint64_t start, uint64_t end);
/* Call only after bitblt returns -2. Measures FIFO polling, never command
 * emission. 0=ready, -1=two-second timeout, -3=GPU error. No frame release. */
int gpu_wait_fifo_ready(void);

#define GPU_BASE              ((u32)0xe1000000u)
#define GPU_CMD_OFFSET        0x00u
#define GPU_STATUS_OFFSET     0x00u
#define GPU_FIFO_STATUS_OFFSET 0x04u

#define GPU_STATUS_FULL       (1u << 0)
#define GPU_STATUS_PROG_FULL  (1u << 1)
#define GPU_STATUS_EMPTY      (1u << 2)
#define GPU_STATUS_BUSY       (1u << 3)
#define GPU_STATUS_DONE       (1u << 4)
#define GPU_STATUS_ERROR      (1u << 5)
#define GPU_FRAME_SWITCH      (1u << 0)
#define GPU_FRAME_NUMBER_MASK (3u << 1)
#define GPU_FRAME_NUMBER_SHIFT 1u

#define FRAMEBUFFER_BASE_1    ((u32)0x06000000u)
#define FRAMEBUFFER_BASE_2    ((u32)0x06200000u)
#define FRAMEBUFFER_BASE_3    ((u32)0x06400000u)
#define FRAME_WIDTH           1280u
#define FRAME_HEIGHT          720u
#define FRAME_STRIDE_BYTES    (FRAME_WIDTH * 2u)
#define GPU_OPERATION_COPY    0u
#define GPU_OPERATION_COLOR   1u
#define GPU_OPERATION_SPRITE  2u
#define GPU_OPERATION_ALPHA   3u
#define GPU_OPERATION_ZOOM    4u
#define GPU_OPERATION_ALPHASPRITE    5u
#define FRAME_STRIDE_BYTE     1u

int bitblt(u32 ctrl, u32 src, u32 dest, u32 width, u32 height,
           u32 src_stride, u32 dest_stride, u32 color);
/* Foreground sole producer only; ISR must never submit. Immutable after prepare.
 * Background is a 1280x720 RGB565 surface. No heap or pixel access. */
typedef struct { u32 src, dest, dims, frame; } GpuPreparedCopy;
int gpu_prepare_copy(GpuPreparedCopy *out, u32 source, u32 x, u32 y,
                     u32 width, u32 height, u32 frame);
int gpu_enqueue_copy(const GpuPreparedCopy *prepared);
int bitblt_wait(u32 timeout);
int frame_switch(u32 timeout);
int frame_pipeline_init(u32 timeout);
void frame_pipeline_poll(void);
void frame_pipeline_tick_isr(void);
int frame_pipeline_acquire(u32 timeout);
int frame_pipeline_submit(u8 frame);
/* Software sampling-opportunity completion, NOT physical HDMI acknowledgement. */
u32 frame_pipeline_consumed_seq(void);
typedef struct {
    u32 software_ticks, consumed_seq, rejected_submit, busy_pending;
    u32 gpu_error, duplicate_irq, rearmed_irq, stale_irq;
    u32 selected_request, retiring, pending, switch_issued;
} FramePipelineStats;
void frame_pipeline_get_stats(FramePipelineStats *out);
int pic_blt(u32 base_addr, u32 src_width, u32 src_height,
            u32 width, u32 height, u32 start_pointx, u32 start_pointy,
            u32 dest_pointx, u32 dest_pointy, u8 frame,u8 cachein ,u8 cachenum);
int pic_colorfill(u32 width, u32 height, u32 dest_pointx, u32 dest_pointy, u8 frame,u16 color);
int pic_sprite(u32 base_addr, u32 src_width, u32 src_height,
        u32 width, u32 height, u32 start_pointx, u32 start_pointy,
        u32 dest_pointx, u32 dest_pointy, u8 frame,u8 cachein ,u8 cachenum);//默认缺省背景值是0x00
int pic_alpha(u32 base_addr, u32 src_width, u32 src_height,
        u32 width, u32 height, u32 start_pointx, u32 start_pointy,
        u32 dest_pointx, u32 dest_pointy, u8 frame,float alpha,u8 cachein ,u8 cachenum);//这里alpha最大分辨率是1/32，也就是适配RGB565
int pic_alphasprite(u32 base_addr, u32 src_width, u32 src_height,
        u32 width, u32 height, u32 start_pointx, u32 start_pointy,
        u32 dest_pointx, u32 dest_pointy, u8 frame,float alpha,u8 cachein ,u8 cachenum);//这里alpha最大分辨率是1/32，也就是适配RGB565
int pic_zoom(u32 base_addr, u32 src_width, u32 src_height,
        u32 width, u32 height, u32 start_pointx, u32 start_pointy,
        u32 dest_pointx, u32 dest_pointy, u8 frame,float zoom);//支持Q2.6格式，也即是两位整数加6位小数
#endif
