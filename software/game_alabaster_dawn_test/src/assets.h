#ifndef G21_ASSETS_H
#define G21_ASSETS_H
#include <stdint.h>
#define G21_FB_W 1280u
#define G21_FB_H 720u
#define G21_FRAME_BYTES 1843200u
#define G21_TRAJECTORIES 64u
#define G21_ISLANDS 18u
#define G21_ROUTE_NODES 17u
#define G21_NO_OPTION 255u
/* Offsets are in uint16 pixels within the decoded image arena. */
typedef struct { uint32_t offset,span_first; uint16_t w,h,span_count; int16_t ax,ay; } AssetImage;
typedef struct { uint16_t y,x,n,pad; } AssetSpan;
typedef struct { int16_t x0,y0,x1,y1,cx,cy; uint32_t mask_offset; } IslandAsset;
typedef struct { uint8_t count,correct_mask,tier,question_id; uint8_t next[3],pad; int16_t ox[3],oy[3]; uint16_t card; } NodeAsset;
typedef struct { uint32_t first; uint16_t count,pad; } TrajAsset;
typedef struct { int16_t x,y; uint16_t cell,flags; } TrajPoint;
typedef struct { uint16_t interval_ms;uint8_t kind,pad;uint16_t traj[4]; } TurretAsset;
typedef struct { uint16_t x,y,end_x;uint8_t kind,pad; } LaserAsset;
typedef struct { int16_t x,y;uint16_t variant;uint8_t visible,kind; } EffectPoint;
extern const AssetImage asset_images[];
extern const AssetSpan asset_spans[];
extern const IslandAsset asset_islands[18];
extern const NodeAsset asset_nodes[2][18];
extern const TrajAsset asset_trajs[64];
extern const TurretAsset asset_turrets[16];
extern const LaserAsset asset_lasers[8];
extern const uint8_t asset_laser_width[180];
extern const uint16_t asset_idle[8],asset_walk[8][45],asset_dodge[8][3],asset_cg[5],asset_stamina[9];
extern const uint16_t asset_labels[3][2],asset_win,asset_gameover,asset_pause;
extern const uint16_t asset_digits[12];
extern const uint16_t asset_perf_labels[2]; /* GPU / DISP~ software estimate */
extern const uint16_t asset_dodge_distance;
extern const uint32_t asset_background_offset,asset_ocean_offset,asset_grey_offset,asset_graywash_offset;
extern const uint32_t asset_fade_offsets[18][9];
extern const uint32_t asset_pixels_count;
extern uint16_t *asset_pixels;
extern const TrajPoint *asset_traj_points;
extern const uint8_t *asset_walk_masks;
extern const EffectPoint *asset_effect_points;
/* Fixed source-cache slots1..6: green/red/blue/right crystal/left crystal/snow. */
extern const uint16_t asset_cache_images[6];
extern const uint16_t asset_effect_images[];
extern const uint16_t asset_effect_frames,asset_effect_count;
int assets_init(void);
int asset_walkable(unsigned island,int x,int y);
const NodeAsset *asset_node(unsigned route,unsigned node);
#endif
