#ifndef UI_H
#define UI_H

#include <citro2d.h>
#include "protocol.h"
#include "animation.h"
#include "settings.h"

// Config screen action results
typedef enum {
    CFG_ACTION_NONE,
    CFG_ACTION_CONFIRM,
    CFG_ACTION_CANCEL
} CfgAction;

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

// Set auto-edit state for rendering
void ui_set_auto_edit(bool enabled);

// Check if touch hit the settings button
int ui_touch_settings(touchPosition touch);

// Scroll tool detail up/down (direction: -1 = up, +1 = down)
void ui_scroll_detail(int direction);

// Config screen: initialize with current settings
void ui_config_init(const AppSettings* settings, bool allow_cancel);

// Config screen: render on bottom screen
void ui_render_config(C3D_RenderTarget* target);

// Config screen: handle input, returns action
CfgAction ui_config_handle_input(u32 kDown);

// Config screen: get edited values
void ui_config_get_values(AppSettings* out);

// Set server info for disconnected screen display
void ui_set_server_info(const char* host, int port);

#endif // UI_H
