#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>

typedef enum {
    STATE_IDLE = 0,
    STATE_WORKING,
    STATE_WAITING,
    STATE_ERROR,
    STATE_DONE
} AgentState;

typedef struct {
    char name[32];
    AgentState state;
    int progress;  // 0-100, -1 for indeterminate
    char message[128];
    char pending_command[256];
    int context_percent;  // 0-100
    bool prompt_visible;
    char prompt_tool_type[64];
    char prompt_tool_detail[1024];
    char prompt_description[256];
    int slot;                   // 0-3, party position
    bool spawning;              // true during pokeball animation
    int spawn_anim_frame;       // animation progress
    bool active;                // true if this slot has a live session
} Agent;

#define MAX_AGENTS 4

#endif // PROTOCOL_H
