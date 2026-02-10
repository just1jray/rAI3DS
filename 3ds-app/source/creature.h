#ifndef CREATURE_H_
#define CREATURE_H_

#include <citro2d.h>

#define CREATURE_W 16
#define CREATURE_SIZE_H 16

typedef struct {
    u32 pixels[CREATURE_SIZE_H][CREATURE_W];
} CreatureFrame;

// Get the idle frame for Clawd (frame 0 = normal, frame 1 = raised 1px)
const CreatureFrame* creature_get_clawd_frame(int frame_index);

// Draw a creature at screen position (x,y) with pixel scale
// Each pixel becomes scale x scale screen pixels
void draw_creature(float x, float y, int scale, const CreatureFrame* frame);

#endif // CREATURE_H_
