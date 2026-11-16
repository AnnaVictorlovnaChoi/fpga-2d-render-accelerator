#ifndef G22_V8_PLATFORM_H
#define G22_V8_PLATFORM_H
/* Aggregation only. GPU API and behavior remain the v8 HDMI_Stress originals. */
#include "bsp.h"
#include "riscv.h"
#include "io.h"
#include "gpio.h"
#include "userDef.h"
#include "2D_DMA.h"
#define AD_CPU_CONFIG_PROFILE "G221_CPU300_I8K_D16K"
#if SYSTEM_CLINT_HZ != 300000000 || SYSTEM_CORES_0_ICACHE_SIZE != 8192 || SYSTEM_CORES_0_DCACHE_SIZE != 16384
#error "G2.21 requires CPU/CLINT 300 MHz, I-cache 8192, D-cache 16384"
#endif
/* Application pacing references, NOT automatic delays or completion fences. */
#define V8_SPRITE_24X24_REFERENCE_US 4u
#define V8_SPRITE_32X32_REFERENCE_US 14u
#endif
