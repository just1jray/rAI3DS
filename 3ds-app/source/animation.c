#include "animation.h"
#include "creature.h"
#include "spritegen.h"
#include <string.h>

// Animation timing definitions
// idle: 2 frames (normal + raised), 20 ticks/frame = ~3Hz
// working: 2 frames (normal + raised), 10 ticks/frame = ~6Hz
// waiting: 2 frames (normal + raised), 8 ticks/frame = ~7.5Hz

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

void anim_generate_sprite(AnimState* state, int slot_index, const char* agent_name) {
    if (!state) return;
    uint32_t seed = spritegen_make_seed(slot_index, agent_name);
    spritegen_create(&state->sprite, seed);
    state->has_sprite = true;
}

const CreatureFrame* anim_current_frame(const AnimState* state) {
    if (!state || !state->current) return NULL;

    // frame_index 0 = normal, frame_index 1 = raised (bob)
    int idx = state->frame_index % 2;
    
    // Use generated sprite if available
    if (state->has_sprite) {
        return spritegen_get_frame(&state->sprite, idx);
    }
    
    // Fallback to legacy clawd frames
    return creature_get_clawd_frame(idx);
}
