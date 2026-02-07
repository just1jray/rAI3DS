#ifndef UI_H
#define UI_H

#include <citro2d.h>
#include "protocol.h"

// Initialize UI resources
void ui_init(void);

// Cleanup UI resources
void ui_exit(void);

// Render top screen with agent dashboard
void ui_render_top(C3D_RenderTarget* target, Agent* agents, int agent_count, int selected, bool connected);

// Render bottom screen with touch controls
void ui_render_bottom(C3D_RenderTarget* target, Agent* selected_agent, bool connected);

// Check if touch is in Yes button
int ui_touch_yes(touchPosition touch);

// Check if touch is in Always button
int ui_touch_always(touchPosition touch);

// Check if touch is in No button
int ui_touch_no(touchPosition touch);

// Check if touch is in Auto-Edit toggle button
int ui_touch_auto_edit(touchPosition touch);

// Set auto-edit state for rendering
void ui_set_auto_edit(bool enabled);

#endif // UI_H
