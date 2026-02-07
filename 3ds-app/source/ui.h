#ifndef UI_H
#define UI_H

#include <citro2d.h>
#include "protocol.h"

// Initialize UI resources
void ui_init(void);

// Cleanup UI resources
void ui_exit(void);

// Render top screen with agent dashboard
void ui_render_top(C3D_RenderTarget* target, Agent* agents, int agent_count, int selected);

// Render bottom screen with touch controls
void ui_render_bottom(C3D_RenderTarget* target, Agent* selected_agent, bool connected);

// Check if touch is in approve button (returns 1 if yes)
int ui_touch_approve(touchPosition touch);

// Check if touch is in deny button (returns 1 if yes)
int ui_touch_deny(touchPosition touch);

#endif // UI_H
