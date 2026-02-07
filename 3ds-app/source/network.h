#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include "protocol.h"

// Initialize network (call once at startup)
bool network_init(void);

// Cleanup network
void network_exit(void);

// Connect to companion server
// Returns true if connection initiated (async)
bool network_connect(const char* host, int port);

// Disconnect from server
void network_disconnect(void);

// Check if connected
bool network_is_connected(void);

// Poll for incoming messages (call every frame)
// Updates agents array with received status
void network_poll(Agent* agents, int* agent_count);

// Send action to server
void network_send_action(const char* agent, const char* action);

// Send command to server
void network_send_command(const char* agent, const char* command);

// Send config change to server (e.g. auto-edit toggle)
void network_send_config(const char* agent, bool auto_edit);

// Get server-synced auto-edit state (updated from broadcasts)
bool network_get_auto_edit(void);

#endif // NETWORK_H
