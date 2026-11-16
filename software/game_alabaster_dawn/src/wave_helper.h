#ifndef AD_G212_WAVE_HELPER_H
#define AD_G212_WAVE_HELPER_H
/* One map-space altar anchor. Screen positions and collision derive from it. */
enum { AD_ALTAR_X=640, AD_ALTAR_Y=328, AD_MAP_SCREEN_Y=50,
       AD_ALTAR_FOOT_HALF_W=20, AD_ALTAR_FOOT_H=12,
       AD_ALTAR_NEAR=60, AD_ALTAR_HINT_GAP=8 };
static inline int altar_screen_y(void){return AD_ALTAR_Y+AD_MAP_SCREEN_Y;}
/* Resource anchors are Q8, allowing hint bottom=altar top-gap, without
 * scattering the previous art-specific 102px offset or relocation delta. */
int altar_hint_center_q8(void);
#endif
