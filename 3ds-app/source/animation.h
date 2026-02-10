#ifndef ANIMATION_H
#define ANIMATION_H

#include <stdbool.h>
#include "creature.h"

typedef struct {
    const CreatureFrame* frames;
    int frame_count;
    int ticks_per_frame;   // e.g. 20 ticks = ~3Hz at 60fps
    bool one_shot;         // for spawn animation — stops at last frame
} AnimDef;

typedef struct {
    const AnimDef* current;
    int frame_index;
    int tick_counter;
    bool finished;         // true when one_shot completes
} AnimState;

// Animation definitions for each agent state
extern const AnimDef anim_idle;       // gentle bob ~3Hz
extern const AnimDef anim_working;    // pulse ~6Hz
extern const AnimDef anim_waiting;    // urgent flash ~7.5Hz
extern const AnimDef anim_spawn;      // pokeball one-shot ~1.5s

// Advance animation by one tick (call once per frame at 60fps)
void anim_tick(AnimState* state);

// Switch to a new animation definition, resetting state
void anim_set(AnimState* state, const AnimDef* def);

// Get the current frame to render
const CreatureFrame* anim_current_frame(const AnimState* state);

#endif // ANIMATION_H
