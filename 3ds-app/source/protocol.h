#ifndef PROTOCOL_H
#define PROTOCOL_H

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
} Agent;

#define MAX_AGENTS 4

#endif // PROTOCOL_H
