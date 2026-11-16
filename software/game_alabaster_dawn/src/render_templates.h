#ifndef AD_RENDER_TEMPLATES_H
#define AD_RENDER_TEMPLATES_H
#include <stdint.h>
#include "enemy_data.h"
/* dst_offset is in pixels, keyed strictly to stride 1280. */
typedef struct { uint32_t source_offset,dst_offset; uint16_t w,h; uint8_t plane,alpha,opt,op; } RenderTemplateOp;
typedef struct { uint16_t first,count; } RenderTemplateRange;
const RenderTemplateOp *render_template_enemy(unsigned frame,unsigned alpha,unsigned stride,unsigned *count);
/* Validate PC metadata against bound resource metadata, never read image pixels. */
int render_template_validate(unsigned frame,const EnemyFrame*,const EnemyPlane*);
#endif
