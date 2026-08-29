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

// FIGHT wheel option kinds (for visual styling)
typedef enum {
    OPT_KIND_STEER = 0,
    OPT_KIND_QUESTION,
    OPT_KIND_ACTION,
    OPT_KIND_META
} OptionKind;

// FIGHT wheel option (Mass Effect paraphrase / Pokémon move style)
#define MAX_OPTIONS 6
#define OPTION_LABEL_LEN 32
#define OPTION_PROMPT_LEN 128

typedef struct {
    int index;                          // 0-5 position
    char label[OPTION_LABEL_LEN];       // Short display text
    char full_prompt[OPTION_PROMPT_LEN]; // Full prompt to send
    OptionKind kind;                    // Visual style hint
} FightOption;

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
    // FIGHT wheel
    FightOption options[MAX_OPTIONS];
    int option_count;           // Number of valid options (0-6)
    char last_beat[64];         // Last agent action (for top screen status)
} Agent;

#define MAX_AGENTS 4

#endif // PROTOCOL_H
