#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
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
static bool network_ready = false;       // network_init() succeeded
static bool first_connection_done = false;  // defer first connect until after first frame (avoids blocking on real 3DS)
static bool auto_edit = false;           // auto-accept Edit/Write tools

int main(int argc, char* argv[]) {
    // Initialize services
    gfxInitDefault();
    aptSetHomeAllowed(true);  // Allow HOME button to return to system menu
    gfxSet3D(false);  // 2D app: disable parallax on real hardware
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // Create render targets
    C3D_RenderTarget* topScreen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottomScreen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // Initialize UI and network
    ui_init();

    network_ready = network_init();
    if (!network_ready)
        printf("Network init failed!\n");
    /* First connection is done after first frame so we don't block gfx on real 3DS (DNS/connect can hang). */

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

        // Auto-accept edits: if prompt is for Edit/Write and toggle is on, auto-approve
        if (auto_edit && agents[selectedAgent].prompt_visible) {
            const char* tt = agents[selectedAgent].prompt_tool_type;
            if (strcasecmp(tt, "Edit") == 0 || strcasecmp(tt, "Write") == 0 ||
                strcasecmp(tt, "NotebookEdit") == 0) {
                printf("Auto-accepting edit: %s\n", tt);
                network_send_action(agents[selectedAgent].name, "yes");
            }
        }

        // Handle touch
        if (kDown & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);

            if (ui_touch_auto_edit(touch)) {
                auto_edit = !auto_edit;
                ui_set_auto_edit(auto_edit);
                printf("Auto-edit: %s\n", auto_edit ? "ON" : "OFF");
            } else if (agents[selectedAgent].prompt_visible) {
                if (ui_touch_yes(touch)) {
                    printf("Sending yes\n");
                    network_send_action(agents[selectedAgent].name, "yes");
                } else if (ui_touch_always(touch)) {
                    printf("Sending always\n");
                    network_send_action(agents[selectedAgent].name, "always");
                } else if (ui_touch_no(touch)) {
                    printf("Sending no\n");
                    network_send_action(agents[selectedAgent].name, "no");
                }
            }
        }

        // D-pad to switch agents
        if (kDown & KEY_DOWN && agent_count > 0) {
            selectedAgent = (selectedAgent + 1) % agent_count;
        }
        if (kDown & KEY_UP && agent_count > 0) {
            selectedAgent = (selectedAgent - 1 + agent_count) % agent_count;
        }

        // Render (always draw first so real 3DS shows UI before any blocking connect)
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        ui_render_top(topScreen, agents, agent_count, selectedAgent);
        ui_render_bottom(bottomScreen,
            agent_count > 0 ? &agents[selectedAgent] : NULL,
            network_is_connected());
        C3D_FrameEnd(0);

        // Deferred first connection: after first frame so hardware doesn't block before any draw
        if (network_ready && !first_connection_done) {
            first_connection_done = true;
            printf("Connecting to %s:%d...\n", SERVER_HOST, SERVER_PORT);
            network_connect(SERVER_HOST, SERVER_PORT);
        }
    }

    // Cleanup
    network_exit();
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
