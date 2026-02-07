#include "ui.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

// Colors
static u32 clrBackground;
static u32 clrWhite;
static u32 clrGray;
static u32 clrGreen;
static u32 clrRed;
static u32 clrYellow;
static u32 clrBlue;
static u32 clrDarkGray;

// Text buffers
static C2D_TextBuf textBuf;

// Screen dimensions
#define TOP_WIDTH 400
#define TOP_HEIGHT 240
#define BOT_WIDTH 320
#define BOT_HEIGHT 240

// 3-button layout: 95px wide, 8px gaps, centered in 320px
// Total: 95*3 + 8*2 = 301, left margin = (320-301)/2 = ~10
#define BTN_Y       15
#define BTN_H       65
#define BTN_W       95
#define BTN_GAP     8
#define BTN_LEFT    10

#define BTN_YES_X     BTN_LEFT
#define BTN_ALWAYS_X  (BTN_LEFT + BTN_W + BTN_GAP)
#define BTN_NO_X      (BTN_LEFT + 2*(BTN_W + BTN_GAP))

// Tool detail area
#define DETAIL_Y      90
#define DETAIL_X      10
#define DETAIL_W      300
#define DETAIL_H      68

// Auto-edit toggle button
#define AUTO_EDIT_X   10
#define AUTO_EDIT_Y   165
#define AUTO_EDIT_W   300
#define AUTO_EDIT_H   30

static bool auto_edit_enabled = false;

void ui_init(void) {
    clrBackground = C2D_Color32(0x2a, 0x2a, 0x4a, 0xFF);
    clrWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
    clrGray = C2D_Color32(0x88, 0x88, 0x88, 0xFF);
    clrGreen = C2D_Color32(0x4C, 0xAF, 0x50, 0xFF);
    clrRed = C2D_Color32(0xF4, 0x43, 0x36, 0xFF);
    clrYellow = C2D_Color32(0xFF, 0xC1, 0x07, 0xFF);
    clrBlue = C2D_Color32(0x21, 0x96, 0xF3, 0xFF);
    clrDarkGray = C2D_Color32(0x44, 0x44, 0x44, 0xFF);

    textBuf = C2D_TextBufNew(2048);
}

void ui_exit(void) {
    C2D_TextBufDelete(textBuf);
}

static u32 state_to_color(AgentState state) {
    switch (state) {
        case STATE_WORKING: return clrBlue;
        case STATE_WAITING: return clrYellow;
        case STATE_ERROR:   return clrRed;
        case STATE_DONE:    return clrGreen;
        default:            return clrGray;
    }
}

static const char* state_to_string(AgentState state) {
    switch (state) {
        case STATE_WORKING: return "Working";
        case STATE_WAITING: return "Waiting";
        case STATE_ERROR:   return "Error";
        case STATE_DONE:    return "Done";
        default:            return "Idle";
    }
}

static void draw_bar(float x, float y, float w, float h, int percent, u32 color) {
    // Background
    C2D_DrawRectSolid(x, y, 0, w, h, C2D_Color32(0x33, 0x33, 0x33, 0xFF));

    // Fill
    if (percent > 0 && percent <= 100) {
        float fillW = (w * percent) / 100.0f;
        C2D_DrawRectSolid(x, y, 0, fillW, h, color);
    }

    // Border
    C2D_DrawRectSolid(x, y, 0, w, 2, clrWhite);
    C2D_DrawRectSolid(x, y + h - 2, 0, w, 2, clrWhite);
    C2D_DrawRectSolid(x, y, 0, 2, h, clrWhite);
    C2D_DrawRectSolid(x + w - 2, y, 0, 2, h, clrWhite);
}

static u32 context_color(int percent) {
    if (percent > 80) return C2D_Color32(0xF4, 0x43, 0x36, 0xFF);  // red
    if (percent > 50) return C2D_Color32(0xFF, 0xC1, 0x07, 0xFF);  // yellow
    return C2D_Color32(0x4C, 0xAF, 0x50, 0xFF);                    // green
}

void ui_render_top(C3D_RenderTarget* target, Agent* agents, int agent_count, int selected) {
    C2D_TargetClear(target, clrBackground);
    C2D_SceneBegin(target);

    C2D_TextBufClear(textBuf);

    float row_height = 55.0f;
    float start_y = 10.0f;

    for (int i = 0; i < agent_count && i < MAX_AGENTS; i++) {
        Agent* agent = &agents[i];
        float y = start_y + (i * row_height);
        u32 stateColor = state_to_color(agent->state);

        // Selection highlight
        if (i == selected) {
            C2D_DrawRectSolid(0, y, 0, TOP_WIDTH, row_height - 5, C2D_Color32(0x2a, 0x2a, 0x4e, 0xFF));
        }

        // Agent name
        C2D_Text txtName;
        char nameBuf[64];
        snprintf(nameBuf, sizeof(nameBuf), "%s", agent->name);
        C2D_TextParse(&txtName, textBuf, nameBuf);
        C2D_TextOptimize(&txtName);
        C2D_DrawText(&txtName, C2D_WithColor, 10, y + 5, 0, 0.6f, 0.6f, clrWhite);

        // State label
        C2D_Text txtState;
        C2D_TextParse(&txtState, textBuf, state_to_string(agent->state));
        C2D_TextOptimize(&txtState);
        C2D_DrawText(&txtState, C2D_WithColor, 320, y + 5, 0, 0.5f, 0.5f, stateColor);

        // Context bar
        char ctxLabel[32];
        snprintf(ctxLabel, sizeof(ctxLabel), "Context: %d%%", agent->context_percent);
        C2D_Text txtCtx;
        C2D_TextParse(&txtCtx, textBuf, ctxLabel);
        C2D_TextOptimize(&txtCtx);
        C2D_DrawText(&txtCtx, C2D_WithColor, 10, y + 22, 0, 0.4f, 0.4f, clrGray);
        draw_bar(100, y + 23, 210, 10, agent->context_percent, context_color(agent->context_percent));

        // Tool info or message below context bar
        if (agent->prompt_tool_type[0] != '\0') {
            // Show tool type in yellow
            C2D_Text txtTool;
            char toolBuf[80];
            if (agent->prompt_tool_detail[0] != '\0') {
                snprintf(toolBuf, sizeof(toolBuf), "%.30s: %.40s", agent->prompt_tool_type, agent->prompt_tool_detail);
            } else {
                snprintf(toolBuf, sizeof(toolBuf), "%.70s", agent->prompt_tool_type);
            }
            C2D_TextParse(&txtTool, textBuf, toolBuf);
            C2D_TextOptimize(&txtTool);
            C2D_DrawText(&txtTool, C2D_WithColor, 10, y + 38, 0, 0.4f, 0.4f, clrYellow);
        } else {
            // Fallback: show message
            C2D_Text txtMsg;
            char msgBuf[64];
            snprintf(msgBuf, sizeof(msgBuf), "%.50s", agent->message);
            C2D_TextParse(&txtMsg, textBuf, msgBuf);
            C2D_TextOptimize(&txtMsg);
            C2D_DrawText(&txtMsg, C2D_WithColor, 10, y + 40, 0, 0.45f, 0.45f, clrGray);
        }

        // Separator line
        C2D_DrawRectSolid(0, y + row_height - 5, 0, TOP_WIDTH, 1, C2D_Color32(0x33, 0x33, 0x33, 0xFF));
    }

    // Title bar at bottom
    C2D_DrawRectSolid(0, TOP_HEIGHT - 20, 0, TOP_WIDTH, 20, C2D_Color32(0x0f, 0x0f, 0x1f, 0xFF));
    C2D_Text txtTitle;
    C2D_TextParse(&txtTitle, textBuf, "rAI3DS v0.1.0");
    C2D_TextOptimize(&txtTitle);
    C2D_DrawText(&txtTitle, C2D_WithColor, 160, TOP_HEIGHT - 17, 0, 0.5f, 0.5f, clrGray);
}

void ui_render_bottom(C3D_RenderTarget* target, Agent* selected_agent, bool connected) {
    C2D_TargetClear(target, clrBackground);
    C2D_SceneBegin(target);

    C2D_TextBufClear(textBuf);

    // Connection status
    if (!connected) {
        C2D_Text txtDisc;
        C2D_TextParse(&txtDisc, textBuf, "Connecting...");
        C2D_TextOptimize(&txtDisc);
        C2D_DrawText(&txtDisc, C2D_WithColor, 90, 95, 0, 0.8f, 0.8f, clrYellow);

        char addrBuf[64];
        snprintf(addrBuf, sizeof(addrBuf), "%s:%d", SERVER_HOST, SERVER_PORT);
        C2D_Text txtAddr;
        C2D_TextParse(&txtAddr, textBuf, addrBuf);
        C2D_TextOptimize(&txtAddr);
        C2D_DrawText(&txtAddr, C2D_WithColor, 40, 120, 0, 0.5f, 0.5f, clrGray);

        C2D_Text txtWait;
        C2D_TextParse(&txtWait, textBuf, "First connect may take 30s");
        C2D_TextOptimize(&txtWait);
        C2D_DrawText(&txtWait, C2D_WithColor, 55, 145, 0, 0.45f, 0.45f, clrGray);

        C2D_Text txtExit;
        C2D_TextParse(&txtExit, textBuf, "START or HOME to exit");
        C2D_TextOptimize(&txtExit);
        C2D_DrawText(&txtExit, C2D_WithColor, 70, 180, 0, 0.5f, 0.5f, clrGray);
        return;
    }

    bool prompt = selected_agent && selected_agent->prompt_visible;

    // YES button
    u32 yesColor = prompt ? clrGreen : clrDarkGray;
    C2D_DrawRectSolid(BTN_YES_X, BTN_Y, 0, BTN_W, BTN_H, yesColor);
    C2D_Text txtYes;
    C2D_TextParse(&txtYes, textBuf, "YES");
    C2D_TextOptimize(&txtYes);
    C2D_DrawText(&txtYes, C2D_WithColor, BTN_YES_X + 28, BTN_Y + 22, 0, 0.75f, 0.75f, clrWhite);

    // ALWAYS button
    u32 alwaysColor = prompt ? clrBlue : clrDarkGray;
    C2D_DrawRectSolid(BTN_ALWAYS_X, BTN_Y, 0, BTN_W, BTN_H, alwaysColor);
    C2D_Text txtAlways;
    C2D_TextParse(&txtAlways, textBuf, "ALWAYS");
    C2D_TextOptimize(&txtAlways);
    C2D_DrawText(&txtAlways, C2D_WithColor, BTN_ALWAYS_X + 13, BTN_Y + 22, 0, 0.7f, 0.7f, clrWhite);

    // NO button
    u32 noColor = prompt ? clrRed : clrDarkGray;
    C2D_DrawRectSolid(BTN_NO_X, BTN_Y, 0, BTN_W, BTN_H, noColor);
    C2D_Text txtNo;
    C2D_TextParse(&txtNo, textBuf, "NO");
    C2D_TextOptimize(&txtNo);
    C2D_DrawText(&txtNo, C2D_WithColor, BTN_NO_X + 33, BTN_Y + 22, 0, 0.75f, 0.75f, clrWhite);

    // Tool detail area
    if (selected_agent && selected_agent->prompt_tool_type[0] != '\0') {
        C2D_DrawRectSolid(DETAIL_X, DETAIL_Y, 0, DETAIL_W, DETAIL_H, C2D_Color32(0x1a, 0x1a, 0x3a, 0xFF));

        // Tool type (yellow)
        C2D_Text txtToolType;
        char typeBuf[64];
        snprintf(typeBuf, sizeof(typeBuf), "%.60s", selected_agent->prompt_tool_type);
        C2D_TextParse(&txtToolType, textBuf, typeBuf);
        C2D_TextOptimize(&txtToolType);
        C2D_DrawText(&txtToolType, C2D_WithColor, DETAIL_X + 5, DETAIL_Y + 4, 0, 0.5f, 0.5f, clrYellow);

        // Separator line
        C2D_DrawRectSolid(DETAIL_X, DETAIL_Y + 22, 0, DETAIL_W, 1, clrDarkGray);

        // Tool detail (white)
        if (selected_agent->prompt_tool_detail[0] != '\0') {
            C2D_Text txtDetail;
            char detailBuf[64];
            snprintf(detailBuf, sizeof(detailBuf), "%.55s", selected_agent->prompt_tool_detail);
            C2D_TextParse(&txtDetail, textBuf, detailBuf);
            C2D_TextOptimize(&txtDetail);
            C2D_DrawText(&txtDetail, C2D_WithColor, DETAIL_X + 5, DETAIL_Y + 27, 0, 0.45f, 0.45f, clrWhite);
        }

        // Description (gray)
        if (selected_agent->prompt_description[0] != '\0') {
            C2D_Text txtDesc;
            char descBuf[64];
            snprintf(descBuf, sizeof(descBuf), "%.55s", selected_agent->prompt_description);
            C2D_TextParse(&txtDesc, textBuf, descBuf);
            C2D_TextOptimize(&txtDesc);
            C2D_DrawText(&txtDesc, C2D_WithColor, DETAIL_X + 5, DETAIL_Y + 46, 0, 0.4f, 0.4f, clrGray);
        }
    } else if (selected_agent && selected_agent->message[0] != '\0') {
        // No prompt visible — show agent message in the detail area
        C2D_DrawRectSolid(DETAIL_X, DETAIL_Y, 0, DETAIL_W, DETAIL_H, C2D_Color32(0x1a, 0x1a, 0x3a, 0xFF));
        C2D_Text txtMsg;
        char msgBuf[64];
        snprintf(msgBuf, sizeof(msgBuf), "%.55s", selected_agent->message);
        C2D_TextParse(&txtMsg, textBuf, msgBuf);
        C2D_TextOptimize(&txtMsg);
        C2D_DrawText(&txtMsg, C2D_WithColor, DETAIL_X + 5, DETAIL_Y + 25, 0, 0.45f, 0.45f, clrGray);
    }

    // Auto-edit toggle button
    u32 aeColor = auto_edit_enabled ? clrGreen : clrDarkGray;
    C2D_DrawRectSolid(AUTO_EDIT_X, AUTO_EDIT_Y, 0, AUTO_EDIT_W, AUTO_EDIT_H, aeColor);
    C2D_Text txtAutoEdit;
    const char* aeLabel = auto_edit_enabled ? "AUTO-ACCEPT EDITS: ON" : "AUTO-ACCEPT EDITS: OFF";
    C2D_TextParse(&txtAutoEdit, textBuf, aeLabel);
    C2D_TextOptimize(&txtAutoEdit);
    C2D_DrawText(&txtAutoEdit, C2D_WithColor, AUTO_EDIT_X + 55, AUTO_EDIT_Y + 7, 0, 0.55f, 0.55f, clrWhite);

    // Agent tabs at bottom
    float tabWidth = BOT_WIDTH / 4.0f;
    const char* agentNames[] = {"Claude", "Codex", "Gemini", "Cursor"};
    for (int i = 0; i < 4; i++) {
        float x = i * tabWidth;
        u32 tabColor = (i == 0) ? clrBlue : C2D_Color32(0x33, 0x33, 0x33, 0xFF);
        C2D_DrawRectSolid(x, BOT_HEIGHT - 30, 0, tabWidth - 2, 30, tabColor);

        C2D_Text txtTab;
        C2D_TextParse(&txtTab, textBuf, agentNames[i]);
        C2D_TextOptimize(&txtTab);
        C2D_DrawText(&txtTab, C2D_WithColor, x + 15, BOT_HEIGHT - 22, 0, 0.5f, 0.5f, clrWhite);
    }
}

int ui_touch_yes(touchPosition touch) {
    return (touch.px >= BTN_YES_X && touch.px <= BTN_YES_X + BTN_W &&
            touch.py >= BTN_Y && touch.py <= BTN_Y + BTN_H);
}

int ui_touch_always(touchPosition touch) {
    return (touch.px >= BTN_ALWAYS_X && touch.px <= BTN_ALWAYS_X + BTN_W &&
            touch.py >= BTN_Y && touch.py <= BTN_Y + BTN_H);
}

int ui_touch_no(touchPosition touch) {
    return (touch.px >= BTN_NO_X && touch.px <= BTN_NO_X + BTN_W &&
            touch.py >= BTN_Y && touch.py <= BTN_Y + BTN_H);
}

int ui_touch_auto_edit(touchPosition touch) {
    return (touch.px >= AUTO_EDIT_X && touch.px <= AUTO_EDIT_X + AUTO_EDIT_W &&
            touch.py >= AUTO_EDIT_Y && touch.py <= AUTO_EDIT_Y + AUTO_EDIT_H);
}

void ui_set_auto_edit(bool enabled) {
    auto_edit_enabled = enabled;
}
