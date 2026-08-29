#ifndef SPRITEGEN_H
#define SPRITEGEN_H

#include "creature.h"
#include <stdint.h>

// Archetype IDs
typedef enum {
    ARCHETYPE_CRAB = 0,
    ARCHETYPE_BLOB,
    ARCHETYPE_BOT,
    ARCHETYPE_BIRD,
    ARCHETYPE_SLIME,
    ARCHETYPE_KNIGHT,
    ARCHETYPE_COUNT
} SpriteArchetype;

// Generated sprite pair (idle + working/raised frame)
typedef struct {
    CreatureFrame frame0;  // idle pose
    CreatureFrame frame1;  // raised/working pose (1px bob)
    uint32_t seed;
    SpriteArchetype archetype;
} GeneratedSprite;

// Generate a unique sprite from seed (slot_index + name hash)
// Returns pointer to internal static storage; subsequent calls overwrite
// For persistent storage, caller should copy the result
void spritegen_create(GeneratedSprite* out, uint32_t seed);

// Hash an agent name to a uint32_t seed component
uint32_t spritegen_hash_name(const char* name);

// Combine slot index and name hash into final seed
uint32_t spritegen_make_seed(int slot_index, const char* agent_name);

// Get frame from generated sprite (frame_index 0 or 1)
const CreatureFrame* spritegen_get_frame(const GeneratedSprite* sprite, int frame_index);

// Create an empty/silhouette frame for unoccupied slots
void spritegen_create_empty(CreatureFrame* out);

#endif // SPRITEGEN_H
