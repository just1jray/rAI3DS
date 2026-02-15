#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "ui.h"
#include "protocol.h"
#include "network.h"
#include "config.h"
#include "settings.h"
#include "animation.h"
#include "creature.h"
#include "audio.h"

// App mode
typedef enum {
    MODE_MAIN,
    MODE_SETTINGS
} AppMode;

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

// Settings / config screen state
static AppMode app_mode = MODE_MAIN;
static AppSettings app_settings;
static char server_host[20];

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

    // Load settings from SD card (or use config.h defaults)
    bool has_saved_config = settings_load(&app_settings);
    settings_format_ip(&app_settings, server_host, sizeof(server_host));
    ui_set_server_info(server_host, app_settings.port);

    // If no saved config, force the config screen on first boot
    if (!has_saved_config) {
        app_mode = MODE_SETTINGS;
        ui_config_init(&app_settings, false);  // no cancel on first boot
    }

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

        // Settings screen mode
        if (app_mode == MODE_SETTINGS) {
            CfgAction action = ui_config_handle_input(kDown);
            if (action == CFG_ACTION_CONFIRM) {
                ui_config_get_values(&app_settings);
                settings_save(&app_settings);
                settings_format_ip(&app_settings, server_host, sizeof(server_host));
                ui_set_server_info(server_host, app_settings.port);
                app_mode = MODE_MAIN;
                // Disconnect and reconnect with new IP
                network_disconnect();
                reconnect_timer = RECONNECT_INTERVAL; // trigger immediate reconnect
                printf("Config saved: %s:%d\n", server_host, app_settings.port);
            } else if (action == CFG_ACTION_CANCEL) {
                app_mode = MODE_MAIN;
            }

            // Render: top screen stays normal, bottom shows config
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            ui_render_top(topScreen, agents, agent_count, selectedAgent,
                          network_is_connected(), creature_anims);
            ui_render_config(bottomScreen);
            C3D_FrameEnd(0);
            continue;
        }

        // SELECT button opens settings
        if (kDown & KEY_SELECT) {
            app_mode = MODE_SETTINGS;
            ui_config_init(&app_settings, true);  // allow cancel
            continue;
        }

        // Touch settings button — checked before network code since
        // network_connect() blocks on TCP timeout with a bad IP
        if (kDown & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);
            if (ui_touch_settings(touch)) {
                app_mode = MODE_SETTINGS;
                ui_config_init(&app_settings, true);
                continue;
            }
        }

        // Network polling
        network_poll(agents, &agent_count);

        // Validate selectedAgent points to an active agent
        if (agent_count > 0 && !agents[selectedAgent].active) {
            for (int i = 0; i < agent_count; i++) {
                if (agents[i].active) { selectedAgent = i; break; }
            }
        }

        // Reconnection logic
        if (!network_is_connected()) {
            reconnect_timer++;
            if (reconnect_timer >= RECONNECT_INTERVAL) {
                reconnect_timer = 0;
                printf("Reconnecting to %s:%d...\n", server_host, app_settings.port);
                network_connect(server_host, app_settings.port);
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
                network_send_command(0, "spawn");
            } else if (ui_touch_auto_edit(touch)) {
                auto_edit = !auto_edit;
                ui_set_auto_edit(auto_edit);
                network_send_config(auto_edit);
                printf("Auto-edit: %s\n", auto_edit ? "ON" : "OFF");
            } else if (agents[selectedAgent].state == STATE_WAITING) {
                if (ui_touch_yes(touch)) {
                    printf("Sending yes\n");
                    network_send_action(selectedAgent, "yes");
                } else if (ui_touch_always(touch)) {
                    printf("Sending always\n");
                    network_send_action(selectedAgent, "always");
                } else if (ui_touch_no(touch)) {
                    printf("Sending no\n");
                    network_send_action(selectedAgent, "no");
                }
            }
        }

        // Physical buttons for permission prompts
        if (agents[selectedAgent].state == STATE_WAITING) {
            if (kDown & KEY_A) {
                printf("Button A: yes\n");
                network_send_action(selectedAgent, "yes");
            }
            if (kDown & KEY_B) {
                printf("Button B: no\n");
                network_send_action(selectedAgent, "no");
            }
            if (kDown & KEY_X) {
                printf("Button X: always\n");
                network_send_action(selectedAgent, "always");
            }
        }

        // Y = toggle auto-edit (works anytime)
        if (kDown & KEY_Y) {
            auto_edit = !auto_edit;
            ui_set_auto_edit(auto_edit);
            network_send_config(auto_edit);
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

        // D-pad up/down and L/R to switch agents (skip inactive)
        if ((kDown & (KEY_DOWN | KEY_R)) && agent_count > 0) {
            int start = selectedAgent;
            do {
                selectedAgent = (selectedAgent + 1) % agent_count;
            } while (!agents[selectedAgent].active && selectedAgent != start);
        }
        if ((kDown & (KEY_UP | KEY_L)) && agent_count > 0) {
            int start = selectedAgent;
            do {
                selectedAgent = (selectedAgent - 1 + agent_count) % agent_count;
            } while (!agents[selectedAgent].active && selectedAgent != start);
        }

        // Render (always draw first so real 3DS shows UI before any blocking connect)
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        ui_render_top(topScreen, agents, agent_count, selectedAgent,
                      network_is_connected(), creature_anims);
        ui_render_bottom(bottomScreen, agents, agent_count, selectedAgent,
                         network_is_connected(), creature_anims);
        C3D_FrameEnd(0);

        // Deferred first connection: after first frame so hardware doesn't block before any draw
        // Skip if we're in settings mode (first boot with no config)
        if (network_ready && !first_connection_done && app_mode == MODE_MAIN) {
            first_connection_done = true;
            printf("Connecting to %s:%d...\n", server_host, app_settings.port);
            network_connect(server_host, app_settings.port);
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
