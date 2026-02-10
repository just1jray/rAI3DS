#ifndef UI_H
#define UI_H

#include <citro2d.h>
#include "protocol.h"
#include "animation.h"

// Initialize UI resources
void ui_init(void);

// Cleanup UI resources
void ui_exit(void);

// Render top screen with agent dashboard
void ui_render_top(C3D_RenderTarget* target, Agent* agents, int agent_count,
                   int selected, bool connected, AnimState* anims);

// Render bottom screen with party lineup and touch controls
void ui_render_bottom(C3D_RenderTarget* target, Agent* agents, int agent_count,
                      int selected, bool connected, AnimState* anims);

// Check if touch is in Yes button
int ui_touch_yes(touchPosition touch);

// Check if touch is in Always button
int ui_touch_always(touchPosition touch);

// Check if touch is in No button
int ui_touch_no(touchPosition touch);

// Check if touch is in Auto-Edit toggle button
int ui_touch_auto_edit(touchPosition touch);

// Check if touch hit a creature slot (returns 0-3, or -1 if none)
int ui_touch_creature_slot(touchPosition touch);

// Check if touch hit the spawn "+" button (returns 1 if hit)
int ui_touch_spawn(touchPosition touch);

// Set auto-edit state for rendering
void ui_set_auto_edit(bool enabled);

// Scroll tool detail up/down (direction: -1 = up, +1 = down)
void ui_scroll_detail(int direction);

#endif // UI_H
