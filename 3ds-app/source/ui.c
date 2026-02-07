#include "ui.h"
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

// Text buffers
static C2D_TextBuf textBuf;

// Screen dimensions
#define TOP_WIDTH 400
#define TOP_HEIGHT 240
#define BOT_WIDTH 320
#define BOT_HEIGHT 240

// Button dimensions (bottom screen)
#define BTN_APPROVE_X 20
#define BTN_APPROVE_Y 20
#define BTN_APPROVE_W 130
#define BTN_APPROVE_H 80

#define BTN_DENY_X 170
#define BTN_DENY_Y 20
#define BTN_DENY_W 130
#define BTN_DENY_H 80

void ui_init(void) {
    clrBackground = C2D_Color32(0x1a, 0x1a, 0x2e, 0xFF);
    clrWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
    clrGray = C2D_Color32(0x88, 0x88, 0x88, 0xFF);
    clrGreen = C2D_Color32(0x4C, 0xAF, 0x50, 0xFF);
    clrRed = C2D_Color32(0xF4, 0x43, 0x36, 0xFF);
    clrYellow = C2D_Color32(0xFF, 0xC1, 0x07, 0xFF);
    clrBlue = C2D_Color32(0x21, 0x96, 0xF3, 0xFF);

    textBuf = C2D_TextBufNew(1024);
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

static void draw_progress_bar(float x, float y, float w, float h, int progress, u32 color) {
    // Background
    C2D_DrawRectSolid(x, y, 0, w, h, C2D_Color32(0x33, 0x33, 0x33, 0xFF));

    // Progress fill
    if (progress >= 0 && progress <= 100) {
        float fillW = (w * progress) / 100.0f;
        C2D_DrawRectSolid(x, y, 0, fillW, h, color);
    } else {
        // Indeterminate: pulse effect (simplified: just show 50%)
        C2D_DrawRectSolid(x, y, 0, w * 0.5f, h, color);
    }

    // Border
    C2D_DrawRectSolid(x, y, 0, w, 2, clrWhite);
    C2D_DrawRectSolid(x, y + h - 2, 0, w, 2, clrWhite);
    C2D_DrawRectSolid(x, y, 0, 2, h, clrWhite);
    C2D_DrawRectSolid(x + w - 2, y, 0, 2, h, clrWhite);
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

        // Progress bar
        draw_progress_bar(10, y + 25, 300, 12, agent->progress, stateColor);

        // Message
        C2D_Text txtMsg;
        char msgBuf[64];
        snprintf(msgBuf, sizeof(msgBuf), "%.50s", agent->message);
        C2D_TextParse(&txtMsg, textBuf, msgBuf);
        C2D_TextOptimize(&txtMsg);
        C2D_DrawText(&txtMsg, C2D_WithColor, 10, y + 40, 0, 0.45f, 0.45f, clrGray);

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
        C2D_DrawText(&txtDisc, C2D_WithColor, 110, 110, 0, 0.8f, 0.8f, clrYellow);
        return;
    }

    // Approve button
    u32 approveColor = (selected_agent && selected_agent->state == STATE_WAITING) ? clrGreen : clrGray;
    C2D_DrawRectSolid(BTN_APPROVE_X, BTN_APPROVE_Y, 0, BTN_APPROVE_W, BTN_APPROVE_H, approveColor);
    C2D_Text txtApprove;
    C2D_TextParse(&txtApprove, textBuf, "APPROVE");
    C2D_TextOptimize(&txtApprove);
    C2D_DrawText(&txtApprove, C2D_WithColor, BTN_APPROVE_X + 25, BTN_APPROVE_Y + 30, 0, 0.8f, 0.8f, clrWhite);

    // Deny button
    u32 denyColor = (selected_agent && selected_agent->state == STATE_WAITING) ? clrRed : clrGray;
    C2D_DrawRectSolid(BTN_DENY_X, BTN_DENY_Y, 0, BTN_DENY_W, BTN_DENY_H, denyColor);
    C2D_Text txtDeny;
    C2D_TextParse(&txtDeny, textBuf, "DENY");
    C2D_TextOptimize(&txtDeny);
    C2D_DrawText(&txtDeny, C2D_WithColor, BTN_DENY_X + 40, BTN_DENY_Y + 30, 0, 0.8f, 0.8f, clrWhite);

    // Context area - show pending command
    if (selected_agent && selected_agent->pending_command[0] != '\0') {
        C2D_DrawRectSolid(10, 115, 0, 300, 50, C2D_Color32(0x2a, 0x2a, 0x4e, 0xFF));

        C2D_Text txtCmd;
        char cmdBuf[64];
        snprintf(cmdBuf, sizeof(cmdBuf), "%.55s", selected_agent->pending_command);
        C2D_TextParse(&txtCmd, textBuf, cmdBuf);
        C2D_TextOptimize(&txtCmd);
        C2D_DrawText(&txtCmd, C2D_WithColor, 15, 125, 0, 0.45f, 0.45f, clrWhite);
    }

    // Agent tabs at bottom
    float tabWidth = BOT_WIDTH / 4.0f;
    const char* agentNames[] = {"Claude", "Codex", "Gemini", "Cursor"};
    for (int i = 0; i < 4; i++) {
        float x = i * tabWidth;
        u32 tabColor = (i == 0) ? clrBlue : C2D_Color32(0x33, 0x33, 0x33, 0xFF);  // Only Claude active for MVP
        C2D_DrawRectSolid(x, BOT_HEIGHT - 30, 0, tabWidth - 2, 30, tabColor);

        C2D_Text txtTab;
        C2D_TextParse(&txtTab, textBuf, agentNames[i]);
        C2D_TextOptimize(&txtTab);
        C2D_DrawText(&txtTab, C2D_WithColor, x + 15, BOT_HEIGHT - 22, 0, 0.5f, 0.5f, clrWhite);
    }
}

int ui_touch_approve(touchPosition touch) {
    return (touch.px >= BTN_APPROVE_X && touch.px <= BTN_APPROVE_X + BTN_APPROVE_W &&
            touch.py >= BTN_APPROVE_Y && touch.py <= BTN_APPROVE_Y + BTN_APPROVE_H);
}

int ui_touch_deny(touchPosition touch) {
    return (touch.px >= BTN_DENY_X && touch.px <= BTN_DENY_X + BTN_DENY_W &&
            touch.py >= BTN_DENY_Y && touch.py <= BTN_DENY_Y + BTN_DENY_H);
}
