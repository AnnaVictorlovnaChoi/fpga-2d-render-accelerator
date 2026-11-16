#ifndef AD_PRECOMPUTED_DATA_H
#define AD_PRECOMPUTED_DATA_H
#include <stdint.h>
/* PTR9v1 little endian: magic[8]="PTR9v1\0\0"; fourteen u32:
 * version=1, header_bytes=64, descriptor_bytes=32, descriptor_count=5220,
 * descriptor_offset=64, points_offset, point_count, raw_total, body_crc32,
 * step_ms=20, variants=2, hazard_offset=0, hazard_count=0, reserved=0.
 * body CRC covers bytes [64,raw_total); external raw CRC covers ALL bytes.
 * No serialized pointers or native ABI casts. hazard_first always UINT32_MAX:
 * no offline hazard hints in v1; runtime scene_enemy_pit remains authority.
 * kind 1=wolf,2=gargoyle; side 0=left,1=right,2=bottom.
 * side entry_id=0..429 means y=150+id, x=30/1250; bottom id=0..14
 * x={170,232,294,356,418,480,542,640,738,800,862,924,986,1048,1110},y=605.
 * wolf index=wall*1750+variant*875+side*430+entry_id;
 * garg index=3500+variant*860+side*430+entry_id; wall ignored for garg.
 * flags=1 valid only. Points every 20 ms, starting at exact entry, final
 * point within radius20 of (640,70); never wrap step. ENTRY wall slide
 * exemption only x<60 or x>=1220, until inside play box; ground strict then.
 * Immutable descriptor pointers owned here; invalidated only by reset/bind.
 */
#define PC_ROUTE_COUNT 5220u
#define PC_ROUTE_VARIANTS 2u
#define PC_ROUTE_WOLF 1u
#define PC_ROUTE_GARGOYLE 2u
#define PC_ROUTE_VALID 1u
typedef struct { uint8_t kind,side,variant,wall; uint16_t entry_id,reserved; uint32_t point_first,point_count; uint16_t start_x,start_y,end_x,end_y; uint32_t hazard_first,flags; } PcRoute;
typedef struct { uint32_t find_calls,point_calls,bad_requests,validation_failures; } PcRouteStats;
int precomputed_data_init(void);
/* Test/tool binding borrows immutable bytes until reset: caller owns lifetime. */
int precomputed_data_bind(const void*,uint32_t);
void precomputed_data_reset(void);
const PcRoute *pc_route_find(unsigned kind,unsigned side,unsigned entry_id,unsigned variant,unsigned wall);
int pc_route_point(const PcRoute*,uint32_t step,uint16_t xyout[2]);
/* Trusted borrowed cursor: only init may construct it. Caller must not alter
 * pointers/count independently, and must discard on reset/bind or byte mutation.
 * No pointers may enter pause storage. next_step==point_count is valid empty.
 * Failure leaves output untouched. All points/CRC were verified by bind. */
typedef struct { const unsigned char *next,*end; uint32_t remaining; } PcRouteCursor;
int pc_route_cursor_init(const PcRoute*,uint32_t next_step,PcRouteCursor*);
const PcRouteStats *pc_route_stats(void);
#endif
