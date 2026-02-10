#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "ui.h"
#include "protocol.h"
#include "network.h"
#include "config.h"
#include "animation.h"
#include "creature.h"
#include "audio.h"

// Reconnection timing
#define RECONNECT_INTERVAL 120  // frames (~2 seconds at 60fps)

static Agent agents[MAX_AGENTS];
static int agent_count = 0;
static int selectedAgent = 0;
static int reconnect_timer = 0;
static bool network_ready = false;       // network_init() succeeded
static bool first_connection_done = false;  // defer first connect until after first frame (avoids blocking on real 3DS)
static bool auto_edit = false;           // auto-accept Edit/Write tools
static int scroll_cooldown = 0;          // frame counter for circle pad debounce

// Animation state per creature slot
static AnimState creature_anims[MAX_AGENTS];
static AgentState prev_agent_states[MAX_AGENTS];  // for detecting state transitions

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

    // Initialize audio
    audio_init();

    // Initialize default agent
    strcpy(agents[0].name, "CLAUDE");
    agents[0].state = STATE_IDLE;
    agents[0].progress = -1;
    strcpy(agents[0].message, "Connecting...");
    agents[0].slot = 0;
    agents[0].active = true;
    agent_count = 1;

    // Initialize animation states
    for (int i = 0; i < MAX_AGENTS; i++) {
        anim_set(&creature_anims[i], &anim_idle);
        prev_agent_states[i] = STATE_IDLE;
    }

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

        // Tick animations and detect state transitions
        for (int i = 0; i < agent_count; i++) {
            // Map agent state to animation
            const AnimDef* target_anim = &anim_idle;
            switch (agents[i].state) {
                case STATE_WORKING: target_anim = &anim_working; break;
                case STATE_WAITING: target_anim = &anim_waiting; break;
                default: target_anim = &anim_idle; break;
            }

            // Switch animation if state changed (but not during spawn)
            if (!agents[i].spawning && creature_anims[i].current != target_anim) {
                anim_set(&creature_anims[i], target_anim);
            }

            // Audio beep on transition to WAITING
            if (agents[i].state == STATE_WAITING && prev_agent_states[i] != STATE_WAITING) {
                audio_play_prompt_beep();
            }
            prev_agent_states[i] = agents[i].state;

            anim_tick(&creature_anims[i]);
        }

        // Sync auto-edit state from server broadcasts
        if (network_get_auto_edit() != auto_edit) {
            auto_edit = network_get_auto_edit();
            ui_set_auto_edit(auto_edit);
        }

        // Handle touch
        if (kDown & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);

            // Check creature slot taps first
            int tapped_slot = ui_touch_creature_slot(touch);
            if (tapped_slot >= 0 && tapped_slot < agent_count) {
                selectedAgent = tapped_slot;
                printf("Selected agent slot %d\n", tapped_slot);
            } else if (tapped_slot >= 0 && tapped_slot >= agent_count) {
                // Tapped empty slot — request spawn
                printf("Spawn requested for slot %d\n", tapped_slot);
                network_send_command(agents[0].name, "spawn");
            } else if (ui_touch_spawn(touch)) {
                printf("Spawn button tapped\n");
                network_send_command(agents[0].name, "spawn");
            } else if (ui_touch_auto_edit(touch)) {
                auto_edit = !auto_edit;
                ui_set_auto_edit(auto_edit);
                network_send_config(agents[selectedAgent].name, auto_edit);
                printf("Auto-edit: %s\n", auto_edit ? "ON" : "OFF");
            } else if (agents[selectedAgent].state == STATE_WAITING) {
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

        // Physical buttons for permission prompts
        if (agents[selectedAgent].state == STATE_WAITING) {
            if (kDown & KEY_A) {
                printf("Button A: yes\n");
                network_send_action(agents[selectedAgent].name, "yes");
            }
            if (kDown & KEY_B) {
                printf("Button B: no\n");
                network_send_action(agents[selectedAgent].name, "no");
            }
            if (kDown & KEY_X) {
                printf("Button X: always\n");
                network_send_action(agents[selectedAgent].name, "always");
            }
        }

        // Y = toggle auto-edit (works anytime)
        if (kDown & KEY_Y) {
            auto_edit = !auto_edit;
            ui_set_auto_edit(auto_edit);
            network_send_config(agents[selectedAgent].name, auto_edit);
            printf("Button Y: auto-edit %s\n", auto_edit ? "ON" : "OFF");
        }

        // Circle pad for scrolling tool detail (debounced)
        if (scroll_cooldown > 0) scroll_cooldown--;
        circlePosition cpad;
        hidCircleRead(&cpad);
        if (scroll_cooldown == 0) {
            if (cpad.dy > 40) {
                ui_scroll_detail(-1);  // stick up = scroll up
                scroll_cooldown = 8;   // ~8 frames between scrolls
            } else if (cpad.dy < -40) {
                ui_scroll_detail(1);   // stick down = scroll down
                scroll_cooldown = 8;
            }
        }

        // D-pad left/right for precise single-line scrolling
        if (kDown & KEY_LEFT) {
            ui_scroll_detail(-1);
        }
        if (kDown & KEY_RIGHT) {
            ui_scroll_detail(1);
        }

        // D-pad up/down to switch agents
        if (kDown & KEY_DOWN && agent_count > 0) {
            selectedAgent = (selectedAgent + 1) % agent_count;
        }
        if (kDown & KEY_UP && agent_count > 0) {
            selectedAgent = (selectedAgent - 1 + agent_count) % agent_count;
        }

        // L/R bumpers to cycle selected agent
        if (kDown & KEY_R && agent_count > 0) {
            selectedAgent = (selectedAgent + 1) % agent_count;
        }
        if (kDown & KEY_L && agent_count > 0) {
            selectedAgent = (selectedAgent - 1 + agent_count) % agent_count;
        }

        // Render (always draw first so real 3DS shows UI before any blocking connect)
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        ui_render_top(topScreen, agents, agent_count, selectedAgent,
                      network_is_connected(), creature_anims);
        ui_render_bottom(bottomScreen, agents, agent_count, selectedAgent,
                         network_is_connected(), creature_anims);
        C3D_FrameEnd(0);

        // Deferred first connection: after first frame so hardware doesn't block before any draw
        if (network_ready && !first_connection_done) {
            first_connection_done = true;
            printf("Connecting to %s:%d...\n", SERVER_HOST, SERVER_PORT);
            network_connect(SERVER_HOST, SERVER_PORT);
        }
    }

    // Cleanup
    audio_exit();
    network_exit();
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
