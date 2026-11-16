#ifndef EXTERNAL_ASSETS_H
#define EXTERNAL_ASSETS_H
#include <stdint.h>
/* ADFLASH3 wire format: little endian; never cast untrusted directory structs. */
#define EXTERNAL_PLAYER 1u
#define EXTERNAL_ARROW 2u
#define EXTERNAL_SCENE 3u
#define EXTERNAL_DRAGON 4u
#define EXTERNAL_FLOW 5u
#define EXTERNAL_COUNTDOWN 6u
#define EXTERNAL_ENEMY 7u
#define EXTERNAL_PRECOMPUTED 8u
#define EXTERNAL_WAVE 9u
#define EXTERNAL_HUD 10u
#define EXTERNAL_KIND_MAX EXTERNAL_HUD
#define EXTERNAL_STAGE_BYTES 0x01000000u
#define EXTERNAL_MAX_ENTRIES 512u
typedef struct {uint32_t kind,id,codec,predictor;uint64_t offset,packed,raw;uint32_t alignment,packed_crc,raw_crc,param;} ExternalEntry;
int external_assets_init(void);
/* Host/offline resettable validator only. Capacity may exceed header total.
 * Board production startup has no rebind API; init failure is permanent. */
#if !defined(__riscv)
int external_assets_bind(const void *pack,uint32_t capacity);
#endif
const ExternalEntry *external_find(uint32_t kind,uint32_t id);
const uint8_t *external_get(uint32_t kind);
const uint8_t *external_source(uint32_t kind,uint32_t id);
/* CRC(raw) covers puff output BEFORE startup predictor inversion. */
int external_decode(uint32_t kind,uint32_t id,void *target,uint32_t bytes);
uint32_t external_crc32(const void *data,uint32_t bytes);
uint32_t external_pack_bytes(void);
#endif
