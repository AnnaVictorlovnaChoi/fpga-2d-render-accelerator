#ifndef G24_LOG_CONFIG_H
#define G24_LOG_CONFIG_H
#ifndef G24_SERIAL
#define G24_SERIAL 1
#endif
#if G24_SERIAL
#define G24_LOG(...) bsp_printf(__VA_ARGS__)
#else
/* Arguments are not evaluated. Quiet image has no formatting/transmit path. */
#define G24_LOG(...) ((void)0)
#endif
#endif
