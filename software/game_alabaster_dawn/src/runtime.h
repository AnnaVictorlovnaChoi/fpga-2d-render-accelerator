#ifndef G23_RUNTIME_H
#define G23_RUNTIME_H
#include <stdint.h>
#include "../resources/player/fpga_player.h"
typedef FpgaPlayerFrame RuntimeFrame;
typedef FpgaPlayerGroup RuntimeGroup;
enum { RUNTIME_ACT_IDLE=0, RUNTIME_ACT_WALK=1, RUNTIME_ACT_MELEE=2, RUNTIME_ACT_RANGED=3, RUNTIME_ACT_DODGE=4 };
enum { RUNTIME_DIR_E=0,RUNTIME_DIR_NE,RUNTIME_DIR_N,RUNTIME_DIR_NW,RUNTIME_DIR_W,RUNTIME_DIR_SW,RUNTIME_DIR_S,RUNTIME_DIR_SE };
typedef struct { uint32_t state,start_us,end_us,offset_q8; } RuntimeDodgeStep;
/* Call once before all access. Repeated calls return cached status, never inflate again. */
int runtime_init(void);
typedef struct { const RuntimeGroup *group; const RuntimeFrame *frame; const uint8_t *pixels; uint32_t group_index,frame_index,stride; } RuntimeVisual;
/* Fast path only accepts the actual frame member of a known indexed group. */
int runtime_visual(unsigned group_index,const RuntimeFrame*,RuntimeVisual*);
int runtime_get_visual(uint32_t action,uint32_t dir8,uint32_t elapsed_us,RuntimeVisual*);
uint32_t runtime_visual_queries(void);
const RuntimeGroup *runtime_get_group(uint32_t action,uint32_t dir8);
/* idle/walk loop; attacks and dodge clamp. Times are microseconds. */
const RuntimeFrame *runtime_get_frame(uint32_t action,uint32_t dir8,uint32_t elapsed_us);
/* Frame offsets are LOCAL to group; pointers valid after successful init only. */
const uint8_t *runtime_pixels(const RuntimeGroup *group,const RuntimeFrame *frame);
uint32_t runtime_frame_stride(const RuntimeGroup *group,const RuntimeFrame *frame);
const uint16_t *runtime_map(void); /* RGB565 1280x720, stride 2560 bytes */
const uint8_t *runtime_mask(void); /* U8 1280x630 LOCAL coordinates */
int runtime_walkable(int32_t local_x,int32_t local_y);
uint32_t runtime_action_duration(uint32_t action,uint32_t dir8);
uint32_t runtime_dodge_duration(void);
const RuntimeDodgeStep *runtime_dodge_step(uint32_t elapsed_us);
uint32_t runtime_group_count(void);
const RuntimeGroup *runtime_group_at(uint32_t index);
uint32_t runtime_arena_bytes(void);
extern const char runtime_source_sha256[];
#endif
