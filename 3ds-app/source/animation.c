#include "animation.h"
#include "creature.h"
#include <string.h>

// We store the creature frames used by each animation inline.
// idle: 2 frames (normal + raised), 20 ticks/frame = ~3Hz
// working: 2 frames (normal + raised), 10 ticks/frame = ~6Hz
// waiting: 2 frames (normal + raised), 8 ticks/frame = ~7.5Hz

// For idle/working/waiting, we reference the same clawd pixel data
// but with different tick rates. The caller can apply color tint
// based on the animation type if desired.

// We use creature_get_clawd_frame() to get the actual frame data,
// so AnimDef.frames just needs to hold the frame count metadata.
// We'll store dummy frames and use anim_current_frame() to dispatch.

// Idle animation: gentle bob at ~3Hz
static const CreatureFrame idle_frames_placeholder[2] = {0};
const AnimDef anim_idle = {
    .frames = idle_frames_placeholder,
    .frame_count = 2,
    .ticks_per_frame = 20,  // 60fps / 20 = 3Hz
    .one_shot = false,
};

// Working animation: faster pulse at ~6Hz
static const CreatureFrame working_frames_placeholder[2] = {0};
const AnimDef anim_working = {
    .frames = working_frames_placeholder,
    .frame_count = 2,
    .ticks_per_frame = 10,  // 60fps / 10 = 6Hz
    .one_shot = false,
};

// Waiting animation: urgent flash at ~7.5Hz
static const CreatureFrame waiting_frames_placeholder[2] = {0};
const AnimDef anim_waiting = {
    .frames = waiting_frames_placeholder,
    .frame_count = 2,
    .ticks_per_frame = 8,   // 60fps / 8 = 7.5Hz
    .one_shot = false,
};

// Spawn animation: pokeball one-shot ~1.5s (90 frames at 60fps)
// Uses 6 keyframes: ball grow, split, flash, materialize, settle, idle
static const CreatureFrame spawn_frames_placeholder[6] = {0};
const AnimDef anim_spawn = {
    .frames = spawn_frames_placeholder,
    .frame_count = 6,
    .ticks_per_frame = 15,  // 90 frames / 6 keyframes = 15 ticks each
    .one_shot = true,
};

void anim_tick(AnimState* state) {
    if (!state || !state->current) return;
    if (state->finished) return;

    state->tick_counter++;
    if (state->tick_counter >= state->current->ticks_per_frame) {
        state->tick_counter = 0;
        state->frame_index++;

        if (state->frame_index >= state->current->frame_count) {
            if (state->current->one_shot) {
                state->frame_index = state->current->frame_count - 1;
                state->finished = true;
            } else {
                state->frame_index = 0;
            }
        }
    }
}

void anim_set(AnimState* state, const AnimDef* def) {
    if (!state) return;
    state->current = def;
    state->frame_index = 0;
    state->tick_counter = 0;
    state->finished = false;
}

const CreatureFrame* anim_current_frame(const AnimState* state) {
    if (!state || !state->current) return NULL;

    // For all clawd animations, use the actual clawd pixel data
    // frame_index 0 = normal, frame_index 1 = raised
    int idx = state->frame_index % 2;  // clawd only has 2 visual frames
    return creature_get_clawd_frame(idx);
}
