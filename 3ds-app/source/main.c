#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "protocol.h"

// Mock agents for testing UI
static Agent agents[MAX_AGENTS] = {
    { "CLAUDE CODE", STATE_WAITING, 75, "Waiting for approval", "rm -rf node_modules" },
    { "CODEX", STATE_WORKING, 50, "Running tests...", "" },
    { "GEMINI", STATE_IDLE, -1, "Ready", "" },
    { "CURSOR", STATE_DONE, 100, "Task completed!", "" }
};
static int selectedAgent = 0;
static bool connected = true;  // Mock connected state

int main(int argc, char* argv[]) {
    // Initialize services
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // Create render targets
    C3D_RenderTarget* topScreen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottomScreen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // Initialize UI
    ui_init();

    // Main loop
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_START)
            break;

        // Handle touch
        if (kDown & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);

            if (ui_touch_approve(touch)) {
                printf("Approve pressed!\n");
                agents[selectedAgent].state = STATE_WORKING;
                strcpy(agents[selectedAgent].message, "Approved - executing...");
            } else if (ui_touch_deny(touch)) {
                printf("Deny pressed!\n");
                agents[selectedAgent].state = STATE_IDLE;
                strcpy(agents[selectedAgent].message, "Denied by user");
            }
        }

        // D-pad to switch agents
        if (kDown & KEY_DOWN) {
            selectedAgent = (selectedAgent + 1) % MAX_AGENTS;
        }
        if (kDown & KEY_UP) {
            selectedAgent = (selectedAgent - 1 + MAX_AGENTS) % MAX_AGENTS;
        }

        // Render
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        ui_render_top(topScreen, agents, MAX_AGENTS, selectedAgent);
        ui_render_bottom(bottomScreen, &agents[selectedAgent], connected);
        C3D_FrameEnd(0);
    }

    // Cleanup
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
