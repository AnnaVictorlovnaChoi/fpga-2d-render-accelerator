#ifndef G22_V8_PLATFORM_H
#define G22_V8_PLATFORM_H
/* Aggregation only. GPU API and behavior remain the v8 HDMI_Stress originals. */
#include "bsp.h"
#include "riscv.h"
#include "io.h"
#include "gpio.h"
#include "userDef.h"
#include "2D_DMA.h"
#if SYSTEM_CLINT_HZ != 300000000
#error "G2.3 requires the 300 MHz CLINT definition"
#endif
/* Application pacing references, NOT automatic delays or completion fences. */
#define V8_SPRITE_24X24_REFERENCE_US 4u
#define V8_SPRITE_32X32_REFERENCE_US 14u
#endif
