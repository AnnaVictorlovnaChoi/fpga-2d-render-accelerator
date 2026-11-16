#ifndef ALPHA_SUPPORT_H
#define ALPHA_SUPPORT_H
#include "dragon_data.h"
#include "flow_data.h"
/* Half-open clip relative to this plane's top-left (not frame/screen).
 * Returns 1 iff any source RGB565 != key0 intersects the clip, otherwise 0.
 * p must be a live asset-owned plane pointer, not a copied descriptor.
 * NULL, unbound, foreign/misaligned pointers and empty/outside clips return 0.
 * Metadata + generated const tables only; never reads runtime source pixels.
 * Intended for SPRITE/ALPHASPRITE, NOT unkeyed ALPHA or COPY. */
int alpha_support_dragon(const DragonPlane *p,int x,int y,int w,int h);
int alpha_support_flow(const FlowPlane *p,int x,int y,int w,int h);
#endif
