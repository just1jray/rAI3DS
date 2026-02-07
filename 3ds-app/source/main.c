#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "protocol.h"
#include "network.h"
#include "config.h"

// Reconnection timing
#define RECONNECT_INTERVAL 120  // frames (~2 seconds at 60fps)

static Agent agents[MAX_AGENTS];
static int agent_count = 0;
static int selectedAgent = 0;
static int reconnect_timer = 0;

int main(int argc, char* argv[]) {
    // Initialize services
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // Create render targets
    C3D_RenderTarget* topScreen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottomScreen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // Initialize UI and network
    ui_init();

    if (!network_init()) {
        printf("Network init failed!\n");
    } else {
        printf("Connecting to %s:%d...\n", SERVER_HOST, SERVER_PORT);
        network_connect(SERVER_HOST, SERVER_PORT);
    }

    // Initialize default agent
    strcpy(agents[0].name, "CLAUDE");
    agents[0].state = STATE_IDLE;
    agents[0].progress = -1;
    strcpy(agents[0].message, "Connecting...");
    agent_count = 1;

    // Main loop
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_START)
            break;

        // Network polling
        network_poll(agents, &agent_count);

        // Reconnection logic
        if (!network_is_connected()) {
            reconnect_timer++;
            if (reconnect_timer >= RECONNECT_INTERVAL) {
                reconnect_timer = 0;
                printf("Reconnecting...\n");
                network_connect(SERVER_HOST, SERVER_PORT);
            }
        } else {
            reconnect_timer = 0;
        }

        // Handle touch
        if (kDown & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);

            if (ui_touch_approve(touch) && agents[selectedAgent].state == STATE_WAITING) {
                printf("Sending approve\n");
                network_send_action(agents[selectedAgent].name, "approve");
            } else if (ui_touch_deny(touch) && agents[selectedAgent].state == STATE_WAITING) {
                printf("Sending deny\n");
                network_send_action(agents[selectedAgent].name, "deny");
            }
        }

        // D-pad to switch agents
        if (kDown & KEY_DOWN && agent_count > 0) {
            selectedAgent = (selectedAgent + 1) % agent_count;
        }
        if (kDown & KEY_UP && agent_count > 0) {
            selectedAgent = (selectedAgent - 1 + agent_count) % agent_count;
        }

        // Render
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        ui_render_top(topScreen, agents, agent_count, selectedAgent);
        ui_render_bottom(bottomScreen,
            agent_count > 0 ? &agents[selectedAgent] : NULL,
            network_is_connected());
        C3D_FrameEnd(0);
    }

    // Cleanup
    network_exit();
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
