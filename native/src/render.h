/* render.h — trek.asm port interface (see docs/trek_blueprint.md). */
#ifndef SKY_RENDER_H
#define SKY_RENDER_H

#include "compat.h"

void initvid(void);
void video(int x, int y, int z, const uint8_t *car_ptr,
           int car_inside_tunnel, int surface_relative_z, duint page_seg);

#endif
