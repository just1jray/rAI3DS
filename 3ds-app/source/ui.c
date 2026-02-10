#include "ui.h"
#include "config.h"
#include "creature.h"
#include <stdio.h>
#include <string.h>

// Catppuccin Mocha palette
static u32 clrBase;       // #1e1e2e — screen background
static u32 clrMantle;     // #181825 — card/panel background
static u32 clrCrust;      // #11111b — title/footer bar background
static u32 clrSurface0;   // #313244 — disabled buttons, inactive tabs
static u32 clrSurface1;   // #45475a — borders, separators
static u32 clrSurface2;   // #585b70 — progress bar borders
static u32 clrOverlay0;   // #6c7086 — dimmed/disabled text
static u32 clrSubtext0;   // #a6adc8 — secondary text
static u32 clrSubtext1;   // #bac2de — brighter secondary text
static u32 clrText;       // #cdd6f4 — primary text
static u32 clrBlue;       // #89b4fa — working state
static u32 clrGreen;      // #a6e3a1 — done/approve/YES
static u32 clrRed;        // #f38ba8 — error/deny/NO
static u32 clrYellow;     // #f9e2af — waiting state
static u32 clrPeach;      // #fab387 — tool names
static u32 clrMauve;      // #cba6f7 — accents, active tab
static u32 clrLavender;   // #b4befe — highlights, title
static u32 clrTeal;       // #94e2d5 — healthy context bar
static u32 clrSapphire;   // #74c7ec — info accent

// Text buffers
static C2D_TextBuf textBuf;

// Screen dimensions
#define TOP_WIDTH 400
#define TOP_HEIGHT 240
#define BOT_WIDTH 320
#define BOT_HEIGHT 240

// Bottom screen layout — prompt mode
#define BTN_Y       123
#define BTN_H       65
#define BTN_W       95
#define BTN_GAP     8
#define BTN_LEFT    10

#define BTN_YES_X     BTN_LEFT
#define BTN_ALWAYS_X  (BTN_LEFT + BTN_W + BTN_GAP)
#define BTN_NO_X      (BTN_LEFT + 2*(BTN_W + BTN_GAP))

// Tool detail area — prompt mode
#define DETAIL_Y      58
#define DETAIL_X      10
#define DETAIL_W      300
#define DETAIL_H      60

// Auto-edit toggle button
#define AUTO_EDIT_X   10
#define AUTO_EDIT_Y   195
#define AUTO_EDIT_W   300
#define AUTO_EDIT_H   25

// Party lineup — creature slot dimensions
#define SLOT_W        75
#define SLOT_GAP      5
#define SLOT_COUNT    4

// Creature slot positions (y varies by mode)
#define SLOT_START_X  ((BOT_WIDTH - (SLOT_W * SLOT_COUNT + SLOT_GAP * (SLOT_COUNT - 1))) / 2)

static bool auto_edit_enabled = false;

// Scroll state for tool detail
static int detail_scroll = 0;
static int detail_total_lines = 0;
static char last_tool_detail[1024] = {0};

void ui_init(void) {
    clrBase     = C2D_Color32(0x1e, 0x1e, 0x2e, 0xFF);
    clrMantle   = C2D_Color32(0x18, 0x18, 0x25, 0xFF);
    clrCrust    = C2D_Color32(0x11, 0x11, 0x1b, 0xFF);
    clrSurface0 = C2D_Color32(0x31, 0x32, 0x44, 0xFF);
    clrSurface1 = C2D_Color32(0x45, 0x47, 0x5a, 0xFF);
    clrSurface2 = C2D_Color32(0x58, 0x5b, 0x70, 0xFF);
    clrOverlay0 = C2D_Color32(0x6c, 0x70, 0x86, 0xFF);
    clrSubtext0 = C2D_Color32(0xa6, 0xad, 0xc8, 0xFF);
    clrSubtext1 = C2D_Color32(0xba, 0xc2, 0xde, 0xFF);
    clrText     = C2D_Color32(0xcd, 0xd6, 0xf4, 0xFF);
    clrBlue     = C2D_Color32(0x89, 0xb4, 0xfa, 0xFF);
    clrGreen    = C2D_Color32(0xa6, 0xe3, 0xa1, 0xFF);
    clrRed      = C2D_Color32(0xf3, 0x8b, 0xa8, 0xFF);
    clrYellow   = C2D_Color32(0xf9, 0xe2, 0xaf, 0xFF);
    clrPeach    = C2D_Color32(0xfa, 0xb3, 0x87, 0xFF);
    clrMauve    = C2D_Color32(0xcb, 0xa6, 0xf7, 0xFF);
    clrLavender = C2D_Color32(0xb4, 0xbe, 0xfe, 0xFF);
    clrTeal     = C2D_Color32(0x94, 0xe2, 0xd5, 0xFF);
    clrSapphire = C2D_Color32(0x74, 0xc7, 0xec, 0xFF);

    textBuf = C2D_TextBufNew(4096);
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
        default:            return clrSubtext0;
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

static void draw_border(float x, float y, float w, float h, u32 color) {
    C2D_DrawRectSolid(x, y, 0, w, 1, color);
    C2D_DrawRectSolid(x, y + h - 1, 0, w, 1, color);
    C2D_DrawRectSolid(x, y, 0, 1, h, color);
    C2D_DrawRectSolid(x + w - 1, y, 0, 1, h, color);
}

static void draw_dashed_border(float x, float y, float w, float h, u32 color) {
    // Simple dashed border for empty slots
    float dash = 4, gap = 3;
    // Top and bottom
    for (float dx = 0; dx < w; dx += dash + gap) {
        float dw = dash;
        if (dx + dw > w) dw = w - dx;
        C2D_DrawRectSolid(x + dx, y, 0, dw, 1, color);
        C2D_DrawRectSolid(x + dx, y + h - 1, 0, dw, 1, color);
    }
    // Left and right
    for (float dy = 0; dy < h; dy += dash + gap) {
        float dh = dash;
        if (dy + dh > h) dh = h - dy;
        C2D_DrawRectSolid(x, y + dy, 0, 1, dh, color);
        C2D_DrawRectSolid(x + w - 1, y + dy, 0, 1, dh, color);
    }
}

static void draw_bar(float x, float y, float w, float h, int percent, u32 color) {
    C2D_DrawRectSolid(x, y, 0, w, h, clrMantle);
    if (percent > 0 && percent <= 100) {
        float fillW = (w * percent) / 100.0f;
        C2D_DrawRectSolid(x, y, 0, fillW, h, color);
    }
    draw_border(x, y, w, h, clrSurface2);
}

#define WRAP_MAX_LINES 20
#define WRAP_LINE_LEN  80

static int wrap_text(const char* text, float scale, float max_width_px,
                     char out_lines[WRAP_MAX_LINES][WRAP_LINE_LEN]) {
    float char_width = 13.0f * scale;
    int max_chars = (int)(max_width_px / char_width);
    if (max_chars < 10) max_chars = 10;
    if (max_chars >= WRAP_LINE_LEN) max_chars = WRAP_LINE_LEN - 1;

    int len = (int)strlen(text);
    int pos = 0;
    int line = 0;

    while (pos < len && line < WRAP_MAX_LINES) {
        int remaining = len - pos;
        if (remaining <= max_chars) {
            snprintf(out_lines[line], WRAP_LINE_LEN, "%.*s", remaining, text + pos);
            line++;
            break;
        }

        int break_at = max_chars;
        for (int i = max_chars; i > 0; i--) {
            if (text[pos + i] == ' ') {
                break_at = i;
                break;
            }
        }

        snprintf(out_lines[line], WRAP_LINE_LEN, "%.*s", break_at, text + pos);
        pos += break_at;
        if (pos < len && text[pos] == ' ') pos++;
        line++;
    }

    return line;
}

static void draw_state_pill(float x, float y, AgentState state, float scale) {
    const char* label = state_to_string(state);
    u32 bg = state_to_color(state);
    float text_width = strlen(label) * 13.0f * scale;
    float pill_w = text_width + 12;
    float pill_h = 18 * scale + 4;

    C2D_DrawRectSolid(x, y, 0, pill_w, pill_h, bg);

    C2D_Text txt;
    C2D_TextParse(&txt, textBuf, label);
    C2D_TextOptimize(&txt);
    C2D_DrawText(&txt, C2D_WithColor, x + 6, y + 2, 0, scale, scale, clrCrust);
}

static u32 context_color(int percent) {
    if (percent > 80) return clrRed;
    if (percent > 50) return clrYellow;
    return clrTeal;
}

// Draw a single creature slot (for party lineup)
static void draw_creature_slot(float x, float y, float w, float h,
                                int slot_idx, Agent* agent, bool is_selected,
                                AnimState* anim) {
    // Background
    C2D_DrawRectSolid(x, y, 0, w, h, clrMantle);

    if (agent && agent->active) {
        // Border — mauve if selected, surface1 if not
        draw_border(x, y, w, h, is_selected ? clrMauve : clrSurface1);

        // Draw creature centered in slot
        const CreatureFrame* frame = anim_current_frame(anim);
        if (frame) {
            int scale = (h > 60) ? 3 : 2;
            float cx = x + (w - CREATURE_W * scale) / 2.0f;
            float cy = y + 2;
            draw_creature(cx, cy, scale, frame);
        }

        // Name label below creature
        C2D_Text txtName;
        char nameBuf[16];
        snprintf(nameBuf, sizeof(nameBuf), "%.10s", agent->name);
        C2D_TextParse(&txtName, textBuf, nameBuf);
        C2D_TextOptimize(&txtName);
        float nameScale = 0.35f;
        float nameW = strlen(nameBuf) * 13.0f * nameScale;
        C2D_DrawText(&txtName, C2D_WithColor,
                     x + (w - nameW) / 2.0f, y + h - 14, 0,
                     nameScale, nameScale, clrText);

        // State indicator dot
        u32 dotColor = state_to_color(agent->state);
        C2D_DrawRectSolid(x + w - 8, y + 3, 0, 5, 5, dotColor);
    } else {
        // Empty slot — dashed border with "+" label
        draw_dashed_border(x, y, w, h, clrSurface1);

        C2D_Text txtPlus;
        C2D_TextParse(&txtPlus, textBuf, "+");
        C2D_TextOptimize(&txtPlus);
        C2D_DrawText(&txtPlus, C2D_WithColor,
                     x + w / 2 - 5, y + h / 2 - 10, 0,
                     0.7f, 0.7f, clrOverlay0);
    }
}

// ========== TOP SCREEN ==========

void ui_render_top(C3D_RenderTarget* target, Agent* agents, int agent_count,
                   int selected, bool connected, AnimState* anims) {
    C2D_TargetClear(target, clrBase);
    C2D_SceneBegin(target);
    C2D_TextBufClear(textBuf);

    if (agent_count <= 1) {
        // === Expanded single-agent layout ===
        Agent* agent = (agent_count > 0) ? &agents[0] : NULL;

        // Title bar (y=0, 24px)
        C2D_DrawRectSolid(0, 0, 0, TOP_WIDTH, 24, clrCrust);
        C2D_Text txtTitle;
        C2D_TextParse(&txtTitle, textBuf, "rAI3DS");
        C2D_TextOptimize(&txtTitle);
        C2D_DrawText(&txtTitle, C2D_WithColor, 10, 3, 0, 0.55f, 0.55f, clrLavender);

        C2D_Text txtVer;
        C2D_TextParse(&txtVer, textBuf, "v0.2.0");
        C2D_TextOptimize(&txtVer);
        C2D_DrawText(&txtVer, C2D_WithColor, 350, 5, 0, 0.4f, 0.4f, clrOverlay0);

        C2D_DrawRectSolid(0, 24, 0, TOP_WIDTH, 1, clrSurface1);

        if (!agent) return;

        // Agent header with creature (y=28, 50px)
        C2D_DrawRectSolid(0, 28, 0, TOP_WIDTH, 50, clrMantle);

        // Draw creature in header area
        if (anims) {
            const CreatureFrame* frame = anim_current_frame(&anims[0]);
            if (frame) {
                draw_creature(15, 30, 3, frame);  // scale 3 = 48x48
            }
        }

        // Agent name (right of creature)
        C2D_Text txtName;
        C2D_TextParse(&txtName, textBuf, agent->name);
        C2D_TextOptimize(&txtName);
        C2D_DrawText(&txtName, C2D_WithColor, 70, 36, 0, 0.7f, 0.7f, clrText);

        // State pill
        draw_state_pill(310, 38, agent->state, 0.5f);

        // Context section (y=85)
        C2D_Text txtCtxLabel;
        C2D_TextParse(&txtCtxLabel, textBuf, "Context Window");
        C2D_TextOptimize(&txtCtxLabel);
        C2D_DrawText(&txtCtxLabel, C2D_WithColor, 40, 85, 0, 0.45f, 0.45f, clrSubtext0);

        draw_bar(40, 104, 290, 16, agent->context_percent, context_color(agent->context_percent));

        char pctBuf[8];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", agent->context_percent);
        C2D_Text txtPct;
        C2D_TextParse(&txtPct, textBuf, pctBuf);
        C2D_TextOptimize(&txtPct);
        C2D_DrawText(&txtPct, C2D_WithColor, 340, 105, 0, 0.45f, 0.45f, clrText);

        char tokenBuf[48];
        int tokens_k = (agent->context_percent * 200) / 100;
        snprintf(tokenBuf, sizeof(tokenBuf), "%dk / 200k tokens", tokens_k);
        C2D_Text txtTokens;
        C2D_TextParse(&txtTokens, textBuf, tokenBuf);
        C2D_TextOptimize(&txtTokens);
        C2D_DrawText(&txtTokens, C2D_WithColor, 40, 125, 0, 0.4f, 0.4f, clrOverlay0);

        C2D_DrawRectSolid(10, 145, 0, TOP_WIDTH - 20, 1, clrSurface1);

        // Activity card (y=148, 70px)
        C2D_DrawRectSolid(10, 148, 0, TOP_WIDTH - 20, 70, clrMantle);
        draw_border(10, 148, TOP_WIDTH - 20, 70, clrSurface1);

        // Reset scroll when tool detail changes
        if (strcmp(agent->prompt_tool_detail, last_tool_detail) != 0) {
            strncpy(last_tool_detail, agent->prompt_tool_detail, sizeof(last_tool_detail) - 1);
            last_tool_detail[sizeof(last_tool_detail) - 1] = '\0';
            detail_scroll = 0;
        }

        if (agent->prompt_tool_type[0] != '\0') {
            C2D_Text txtToolLabel;
            C2D_TextParse(&txtToolLabel, textBuf, "Current Tool");
            C2D_TextOptimize(&txtToolLabel);
            C2D_DrawText(&txtToolLabel, C2D_WithColor, 20, 151, 0, 0.4f, 0.4f, clrSubtext0);

            C2D_Text txtToolType;
            C2D_TextParse(&txtToolType, textBuf, agent->prompt_tool_type);
            C2D_TextOptimize(&txtToolType);
            C2D_DrawText(&txtToolType, C2D_WithColor, 20, 163, 0, 0.55f, 0.55f, clrPeach);

            if (agent->prompt_tool_detail[0] != '\0') {
                char lines[WRAP_MAX_LINES][WRAP_LINE_LEN];
                memset(lines, 0, sizeof(lines));
                int nlines = wrap_text(agent->prompt_tool_detail, 0.43f, TOP_WIDTH - 50, lines);
                detail_total_lines = nlines;
                int visible = 3;
                for (int l = 0; l < visible && (l + detail_scroll) < nlines; l++) {
                    C2D_Text txtLine;
                    C2D_TextParse(&txtLine, textBuf, lines[l + detail_scroll]);
                    C2D_TextOptimize(&txtLine);
                    C2D_DrawText(&txtLine, C2D_WithColor, 20, 179 + l * 13, 0, 0.43f, 0.43f, clrText);
                }
                if (detail_scroll + visible < nlines) {
                    C2D_Text txtMore;
                    C2D_TextParse(&txtMore, textBuf, "...");
                    C2D_TextOptimize(&txtMore);
                    C2D_DrawText(&txtMore, C2D_WithColor, 370, 204, 0, 0.4f, 0.4f, clrOverlay0);
                }
            }
        } else {
            C2D_Text txtToolLabel;
            C2D_TextParse(&txtToolLabel, textBuf, "Activity");
            C2D_TextOptimize(&txtToolLabel);
            C2D_DrawText(&txtToolLabel, C2D_WithColor, 20, 151, 0, 0.4f, 0.4f, clrSubtext0);

            C2D_Text txtState;
            char stateBuf[32];
            snprintf(stateBuf, sizeof(stateBuf), "%s...", state_to_string(agent->state));
            C2D_TextParse(&txtState, textBuf, stateBuf);
            C2D_TextOptimize(&txtState);
            C2D_DrawText(&txtState, C2D_WithColor, 20, 170, 0, 0.55f, 0.55f, state_to_color(agent->state));
        }

        // Footer bar
        C2D_DrawRectSolid(0, 220, 0, TOP_WIDTH, 20, clrCrust);

        if (connected) {
            C2D_DrawRectSolid(12, 227, 0, 6, 6, clrGreen);
            C2D_Text txtConn;
            C2D_TextParse(&txtConn, textBuf, "Connected");
            C2D_TextOptimize(&txtConn);
            C2D_DrawText(&txtConn, C2D_WithColor, 22, 223, 0, 0.4f, 0.4f, clrSubtext0);
        } else {
            C2D_DrawRectSolid(12, 227, 0, 6, 6, clrRed);
            C2D_Text txtConn;
            C2D_TextParse(&txtConn, textBuf, "Disconnected");
            C2D_TextOptimize(&txtConn);
            C2D_DrawText(&txtConn, C2D_WithColor, 22, 223, 0, 0.4f, 0.4f, clrSubtext0);
        }

    } else {
        // === Multi-agent compact rows with creatures ===
        float row_height = 55.0f;
        float start_y = 10.0f;

        for (int i = 0; i < agent_count && i < MAX_AGENTS; i++) {
            Agent* agent = &agents[i];
            float y = start_y + (i * row_height);

            if (i == selected) {
                C2D_DrawRectSolid(0, y, 0, TOP_WIDTH, row_height - 5, clrMantle);
            }

            // Small creature on the left
            if (anims) {
                const CreatureFrame* frame = anim_current_frame(&anims[i]);
                if (frame) {
                    draw_creature(5, y + 3, 2, frame);  // scale 2 = 32x32
                }
            }

            C2D_Text txtName;
            C2D_TextParse(&txtName, textBuf, agent->name);
            C2D_TextOptimize(&txtName);
            C2D_DrawText(&txtName, C2D_WithColor, 42, y + 5, 0, 0.6f, 0.6f, clrText);

            C2D_Text txtState;
            C2D_TextParse(&txtState, textBuf, state_to_string(agent->state));
            C2D_TextOptimize(&txtState);
            C2D_DrawText(&txtState, C2D_WithColor, 320, y + 5, 0, 0.5f, 0.5f, state_to_color(agent->state));

            char ctxLabel[32];
            snprintf(ctxLabel, sizeof(ctxLabel), "Context: %d%%", agent->context_percent);
            C2D_Text txtCtx;
            C2D_TextParse(&txtCtx, textBuf, ctxLabel);
            C2D_TextOptimize(&txtCtx);
            C2D_DrawText(&txtCtx, C2D_WithColor, 42, y + 22, 0, 0.4f, 0.4f, clrSubtext0);
            draw_bar(130, y + 23, 180, 10, agent->context_percent, context_color(agent->context_percent));

            if (agent->prompt_tool_type[0] != '\0') {
                C2D_Text txtTool;
                char toolBuf[80];
                if (agent->prompt_tool_detail[0] != '\0') {
                    snprintf(toolBuf, sizeof(toolBuf), "%.30s: %.40s", agent->prompt_tool_type, agent->prompt_tool_detail);
                } else {
                    snprintf(toolBuf, sizeof(toolBuf), "%.70s", agent->prompt_tool_type);
                }
                C2D_TextParse(&txtTool, textBuf, toolBuf);
                C2D_TextOptimize(&txtTool);
                C2D_DrawText(&txtTool, C2D_WithColor, 42, y + 38, 0, 0.4f, 0.4f, clrPeach);
            } else {
                C2D_Text txtMsg;
                C2D_TextParse(&txtMsg, textBuf, state_to_string(agent->state));
                C2D_TextOptimize(&txtMsg);
                C2D_DrawText(&txtMsg, C2D_WithColor, 42, y + 40, 0, 0.45f, 0.45f, clrSubtext0);
            }

            C2D_DrawRectSolid(0, y + row_height - 5, 0, TOP_WIDTH, 1, clrSurface1);
        }

        // Title bar at bottom
        C2D_DrawRectSolid(0, TOP_HEIGHT - 20, 0, TOP_WIDTH, 20, clrCrust);
        C2D_Text txtTitle;
        C2D_TextParse(&txtTitle, textBuf, "rAI3DS v0.2.0");
        C2D_TextOptimize(&txtTitle);
        C2D_DrawText(&txtTitle, C2D_WithColor, 160, TOP_HEIGHT - 17, 0, 0.5f, 0.5f, clrSubtext0);
    }
}

// ========== BOTTOM SCREEN ==========

void ui_render_bottom(C3D_RenderTarget* target, Agent* agents, int agent_count,
                      int selected, bool connected, AnimState* anims) {
    C2D_TargetClear(target, clrBase);
    C2D_SceneBegin(target);
    C2D_TextBufClear(textBuf);

    Agent* selected_agent = (agent_count > 0 && selected < agent_count) ? &agents[selected] : NULL;

    // Connection status — disconnected screen
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
        C2D_DrawText(&txtAddr, C2D_WithColor, 40, 120, 0, 0.5f, 0.5f, clrSubtext0);

        C2D_Text txtWait;
        C2D_TextParse(&txtWait, textBuf, "First connect may take 30s");
        C2D_TextOptimize(&txtWait);
        C2D_DrawText(&txtWait, C2D_WithColor, 55, 145, 0, 0.45f, 0.45f, clrSubtext0);

        C2D_Text txtExit;
        C2D_TextParse(&txtExit, textBuf, "START or HOME to exit");
        C2D_TextOptimize(&txtExit);
        C2D_DrawText(&txtExit, C2D_WithColor, 70, 180, 0, 0.5f, 0.5f, clrSubtext0);
        return;
    }

    bool prompt = selected_agent && selected_agent->state == STATE_WAITING;

    if (prompt) {
        // ========== PROMPT MODE LAYOUT ==========

        // Compact party lineup (y=0-53, scale 2 creatures)
        float slot_h = 53;
        for (int i = 0; i < SLOT_COUNT; i++) {
            float sx = SLOT_START_X + i * (SLOT_W + SLOT_GAP);
            Agent* a = (i < agent_count) ? &agents[i] : NULL;
            draw_creature_slot(sx, 0, SLOT_W, slot_h, i, a, (i == selected),
                              anims ? &anims[i] : NULL);
        }

        // Tool detail card (y=58-118)
        C2D_DrawRectSolid(DETAIL_X, DETAIL_Y, 0, DETAIL_W, DETAIL_H, clrMantle);
        draw_border(DETAIL_X, DETAIL_Y, DETAIL_W, DETAIL_H, clrSurface1);

        if (selected_agent && selected_agent->prompt_tool_type[0] != '\0') {
            C2D_Text txtToolType;
            C2D_TextParse(&txtToolType, textBuf, selected_agent->prompt_tool_type);
            C2D_TextOptimize(&txtToolType);
            C2D_DrawText(&txtToolType, C2D_WithColor, DETAIL_X + 5, DETAIL_Y + 3, 0, 0.45f, 0.45f, clrPeach);

            C2D_DrawRectSolid(DETAIL_X + 5, DETAIL_Y + 18, 0, DETAIL_W - 10, 1, clrSurface1);

            if (selected_agent->prompt_tool_detail[0] != '\0') {
                char lines[WRAP_MAX_LINES][WRAP_LINE_LEN];
                memset(lines, 0, sizeof(lines));
                int nlines = wrap_text(selected_agent->prompt_tool_detail, 0.40f, DETAIL_W - 15, lines);
                detail_total_lines = nlines;
                int visible = 3;
                for (int l = 0; l < visible && (l + detail_scroll) < nlines; l++) {
                    C2D_Text txtLine;
                    C2D_TextParse(&txtLine, textBuf, lines[l + detail_scroll]);
                    C2D_TextOptimize(&txtLine);
                    C2D_DrawText(&txtLine, C2D_WithColor, DETAIL_X + 5, DETAIL_Y + 22 + l * 12, 0,
                                 0.40f, 0.40f, clrText);
                }
                if (detail_scroll + visible < nlines) {
                    C2D_Text txtMore;
                    C2D_TextParse(&txtMore, textBuf, "...");
                    C2D_TextOptimize(&txtMore);
                    C2D_DrawText(&txtMore, C2D_WithColor, DETAIL_X + DETAIL_W - 20, DETAIL_Y + DETAIL_H - 12, 0,
                                 0.35f, 0.35f, clrOverlay0);
                }
            }
        }

        // Action buttons (y=123-188, 65px tall)
        // YES button
        u32 yesBg = clrGreen;
        C2D_DrawRectSolid(BTN_YES_X, BTN_Y, 0, BTN_W, BTN_H, yesBg);
        C2D_DrawRectSolid(BTN_YES_X, BTN_Y, 0, BTN_W, 2, clrSurface2);
        C2D_DrawRectSolid(BTN_YES_X, BTN_Y + BTN_H - 2, 0, BTN_W, 2, clrCrust);
        C2D_Text txtYes;
        C2D_TextParse(&txtYes, textBuf, "YES");
        C2D_TextOptimize(&txtYes);
        C2D_DrawText(&txtYes, C2D_WithColor, BTN_YES_X + 28, BTN_Y + 14, 0, 0.75f, 0.75f, clrCrust);
        // Button hint
        C2D_Text txtYesHint;
        C2D_TextParse(&txtYesHint, textBuf, "[A]");
        C2D_TextOptimize(&txtYesHint);
        C2D_DrawText(&txtYesHint, C2D_WithColor, BTN_YES_X + 33, BTN_Y + 42, 0, 0.4f, 0.4f, clrCrust);

        // ALWAYS button
        u32 alwaysBg = clrBlue;
        C2D_DrawRectSolid(BTN_ALWAYS_X, BTN_Y, 0, BTN_W, BTN_H, alwaysBg);
        C2D_DrawRectSolid(BTN_ALWAYS_X, BTN_Y, 0, BTN_W, 2, clrSurface2);
        C2D_DrawRectSolid(BTN_ALWAYS_X, BTN_Y + BTN_H - 2, 0, BTN_W, 2, clrCrust);
        C2D_Text txtAlways;
        C2D_TextParse(&txtAlways, textBuf, "ALWAYS");
        C2D_TextOptimize(&txtAlways);
        C2D_DrawText(&txtAlways, C2D_WithColor, BTN_ALWAYS_X + 13, BTN_Y + 14, 0, 0.7f, 0.7f, clrCrust);
        C2D_Text txtAlwaysHint;
        C2D_TextParse(&txtAlwaysHint, textBuf, "[X]");
        C2D_TextOptimize(&txtAlwaysHint);
        C2D_DrawText(&txtAlwaysHint, C2D_WithColor, BTN_ALWAYS_X + 33, BTN_Y + 42, 0, 0.4f, 0.4f, clrCrust);

        // NO button
        u32 noBg = clrRed;
        C2D_DrawRectSolid(BTN_NO_X, BTN_Y, 0, BTN_W, BTN_H, noBg);
        C2D_DrawRectSolid(BTN_NO_X, BTN_Y, 0, BTN_W, 2, clrSurface2);
        C2D_DrawRectSolid(BTN_NO_X, BTN_Y + BTN_H - 2, 0, BTN_W, 2, clrCrust);
        C2D_Text txtNo;
        C2D_TextParse(&txtNo, textBuf, "NO");
        C2D_TextOptimize(&txtNo);
        C2D_DrawText(&txtNo, C2D_WithColor, BTN_NO_X + 33, BTN_Y + 14, 0, 0.75f, 0.75f, clrCrust);
        C2D_Text txtNoHint;
        C2D_TextParse(&txtNoHint, textBuf, "[B]");
        C2D_TextOptimize(&txtNoHint);
        C2D_DrawText(&txtNoHint, C2D_WithColor, BTN_NO_X + 33, BTN_Y + 42, 0, 0.4f, 0.4f, clrCrust);

    } else {
        // ========== IDLE MODE LAYOUT ==========

        // Party lineup (y=0-70, creatures at scale 3)
        float slot_h = 70;
        for (int i = 0; i < SLOT_COUNT; i++) {
            float sx = SLOT_START_X + i * (SLOT_W + SLOT_GAP);
            Agent* a = (i < agent_count) ? &agents[i] : NULL;
            draw_creature_slot(sx, 0, SLOT_W, slot_h, i, a, (i == selected),
                              anims ? &anims[i] : NULL);
        }

        // Selected creature showcase (y=75-195)
        if (selected_agent) {
            C2D_DrawRectSolid(10, 75, 0, BOT_WIDTH - 20, 120, clrMantle);
            draw_border(10, 75, BOT_WIDTH - 20, 120, clrSurface1);

            // Large creature (scale 5 = 80x80)
            if (anims) {
                const CreatureFrame* frame = anim_current_frame(&anims[selected]);
                if (frame) {
                    draw_creature(20, 80, 5, frame);
                }
            }

            // Info panel right of creature
            float infoX = 110;

            // Agent name
            C2D_Text txtName;
            C2D_TextParse(&txtName, textBuf, selected_agent->name);
            C2D_TextOptimize(&txtName);
            C2D_DrawText(&txtName, C2D_WithColor, infoX, 80, 0, 0.6f, 0.6f, clrText);

            // State pill
            draw_state_pill(infoX, 98, selected_agent->state, 0.45f);

            // Context bar
            C2D_Text txtCtx;
            C2D_TextParse(&txtCtx, textBuf, "Context");
            C2D_TextOptimize(&txtCtx);
            C2D_DrawText(&txtCtx, C2D_WithColor, infoX, 118, 0, 0.35f, 0.35f, clrSubtext0);
            draw_bar(infoX, 132, 180, 10, selected_agent->context_percent,
                     context_color(selected_agent->context_percent));

            // Current tool info
            if (selected_agent->prompt_tool_type[0] != '\0') {
                C2D_Text txtTool;
                char toolBuf[80];
                snprintf(toolBuf, sizeof(toolBuf), "%.70s", selected_agent->prompt_tool_type);
                C2D_TextParse(&txtTool, textBuf, toolBuf);
                C2D_TextOptimize(&txtTool);
                C2D_DrawText(&txtTool, C2D_WithColor, infoX, 150, 0, 0.4f, 0.4f, clrPeach);

                if (selected_agent->prompt_tool_detail[0] != '\0') {
                    C2D_Text txtDetail;
                    char detBuf[80];
                    snprintf(detBuf, sizeof(detBuf), "%.70s", selected_agent->prompt_tool_detail);
                    C2D_TextParse(&txtDetail, textBuf, detBuf);
                    C2D_TextOptimize(&txtDetail);
                    C2D_DrawText(&txtDetail, C2D_WithColor, infoX, 165, 0, 0.35f, 0.35f, clrText);
                }
            } else {
                C2D_Text txtState;
                char stateBuf[32];
                snprintf(stateBuf, sizeof(stateBuf), "%s...", state_to_string(selected_agent->state));
                C2D_TextParse(&txtState, textBuf, stateBuf);
                C2D_TextOptimize(&txtState);
                C2D_DrawText(&txtState, C2D_WithColor, infoX, 150, 0, 0.5f, 0.5f,
                             state_to_color(selected_agent->state));
            }
        }
    }

    // Auto-edit toggle (always visible at bottom)
    u32 aeColor = auto_edit_enabled ? clrGreen : clrSurface0;
    u32 aeTxt = auto_edit_enabled ? clrCrust : clrSubtext0;
    C2D_DrawRectSolid(AUTO_EDIT_X, AUTO_EDIT_Y, 0, AUTO_EDIT_W, AUTO_EDIT_H, aeColor);
    draw_border(AUTO_EDIT_X, AUTO_EDIT_Y, AUTO_EDIT_W, AUTO_EDIT_H, clrSurface1);
    C2D_Text txtAutoEdit;
    const char* aeLabel = auto_edit_enabled ? "AUTO-ACCEPT EDITS: ON [Y]" : "AUTO-ACCEPT EDITS: OFF [Y]";
    C2D_TextParse(&txtAutoEdit, textBuf, aeLabel);
    C2D_TextOptimize(&txtAutoEdit);
    C2D_DrawText(&txtAutoEdit, C2D_WithColor, AUTO_EDIT_X + 40, AUTO_EDIT_Y + 5, 0, 0.5f, 0.5f, aeTxt);

    // Status bar (y=225-240)
    C2D_DrawRectSolid(0, 225, 0, BOT_WIDTH, 15, clrCrust);
    C2D_Text txtStatus;
    C2D_TextParse(&txtStatus, textBuf, "L/R: Switch   A:Yes B:No X:Always Y:Auto");
    C2D_TextOptimize(&txtStatus);
    C2D_DrawText(&txtStatus, C2D_WithColor, 10, 227, 0, 0.35f, 0.35f, clrOverlay0);
}

// ========== TOUCH ZONES ==========

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

int ui_touch_creature_slot(touchPosition touch) {
    // Check each slot in the party lineup
    // Slot height depends on whether we're in prompt mode (53) or idle (70)
    // Since we don't track mode here, use the larger bounding box (70px)
    float slot_h = 70;
    for (int i = 0; i < SLOT_COUNT; i++) {
        float sx = SLOT_START_X + i * (SLOT_W + SLOT_GAP);
        if (touch.px >= sx && touch.px <= sx + SLOT_W &&
            touch.py >= 0 && touch.py <= slot_h) {
            return i;
        }
    }
    return -1;
}

int ui_touch_spawn(touchPosition touch) {
    // Spawn is triggered by tapping any empty slot (handled in main.c)
    // This function is for a dedicated spawn button, which we don't have yet
    return 0;
}

void ui_set_auto_edit(bool enabled) {
    auto_edit_enabled = enabled;
}

void ui_scroll_detail(int direction) {
    detail_scroll += direction;
    if (detail_scroll < 0) detail_scroll = 0;
    int max_scroll = detail_total_lines - 3;
    if (max_scroll < 0) max_scroll = 0;
    if (detail_scroll > max_scroll) detail_scroll = max_scroll;
}
