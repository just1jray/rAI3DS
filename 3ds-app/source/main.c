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
#include "spritegen.h"
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
static int agent_switch_cooldown = 0;    // frame counter for L/R and D-pad agent switching

// Animation state per creature slot
static AnimState creature_anims[MAX_AGENTS];
static AgentState prev_agent_states[MAX_AGENTS];  // for detecting state transitions
static char prev_agent_names[MAX_AGENTS][32];     // for detecting name changes (sprite regen)

// Find next active agent slot (wraps around, skips inactive)
// Returns current if no other active slots exist
static int find_next_active(int current, int count) {
    if (count <= 0) return 0;
    for (int i = 1; i <= count; i++) {
        int idx = (current + i) % count;
        if (agents[idx].active) return idx;
    }
    return current;  // No other active found, stay put
}

// Find previous active agent slot (wraps around, skips inactive)
static int find_prev_active(int current, int count) {
    if (count <= 0) return 0;
    for (int i = 1; i <= count; i++) {
        int idx = (current - i + count) % count;
        if (agents[idx].active) return idx;
    }
    return current;  // No other active found, stay put
}

// Snap selection to nearest active if current is inactive
static int snap_to_active(int current, int count) {
    if (count <= 0) return 0;
    if (current < count && agents[current].active) return current;
    // Try to find any active slot
    for (int i = 0; i < count; i++) {
        if (agents[i].active) return i;
    }
    return 0;  // Fallback
}

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

    // Initialize animation states and generate sprites
    for (int i = 0; i < MAX_AGENTS; i++) {
        anim_set(&creature_anims[i], &anim_idle);
        prev_agent_states[i] = STATE_IDLE;
        prev_agent_names[i][0] = '\0';
    }
    // Generate sprite for default agent
    anim_generate_sprite(&creature_anims[0], 0, agents[0].name);
    strcpy(prev_agent_names[0], agents[0].name);

    // Main loop
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        if (kDown & KEY_START)
            break;

        // Decrement cooldowns
        if (agent_switch_cooldown > 0) agent_switch_cooldown--;

        // Network polling
        network_poll(agents, &agent_count);

        // Snap selection to active slot if current became inactive
        selectedAgent = snap_to_active(selectedAgent, agent_count);

        // Regenerate sprites when agent names change (new connection)
        for (int i = 0; i < agent_count; i++) {
            if (strcmp(agents[i].name, prev_agent_names[i]) != 0) {
                anim_generate_sprite(&creature_anims[i], i, agents[i].name);
                strncpy(prev_agent_names[i], agents[i].name, sizeof(prev_agent_names[i]) - 1);
                prev_agent_names[i][sizeof(prev_agent_names[i]) - 1] = '\0';
            }
        }

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

        // Determine if we're in permission prompt mode or FIGHT wheel mode
        bool in_prompt = agents[selectedAgent].state == STATE_WAITING;
        // FIGHT mode is active when NOT in prompt - even with zero options (A/B still work)
        bool in_fight = !in_prompt;
        int opt_count = agents[selectedAgent].option_count;

        // Log face button presses for QA/debugging
        if (kDown & (KEY_A | KEY_B | KEY_X | KEY_Y)) {
            char btn_log[64] = {0};
            if (kDown & KEY_A) strcat(btn_log, "A ");
            if (kDown & KEY_B) strcat(btn_log, "B ");
            if (kDown & KEY_X) strcat(btn_log, "X ");
            if (kDown & KEY_Y) strcat(btn_log, "Y ");
            printf("[input] Face buttons: %s (kDown=0x%08lx)\n", btn_log, (unsigned long)kDown);
            ui_set_last_key(btn_log);
        }

        // Handle touch
        if (kDown & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);

            // Check creature slot taps first (always available)
            int tapped_slot = ui_touch_creature_slot(touch);
            if (tapped_slot >= 0 && tapped_slot < agent_count) {
                selectedAgent = tapped_slot;
                printf("Selected agent slot %d\n", tapped_slot);
            } else if (tapped_slot >= 0 && tapped_slot >= agent_count) {
                // Tapped empty slot — request spawn
                printf("Spawn requested for slot %d\n", tapped_slot);
                network_send_command(agents[0].name, "spawn", tapped_slot);
            } else if (ui_touch_spawn(touch)) {
                printf("Spawn button tapped\n");
                network_send_command(agents[0].name, "spawn", selectedAgent);
            } else if (ui_touch_auto_edit(touch)) {
                auto_edit = !auto_edit;
                ui_set_auto_edit(auto_edit);
                network_send_config(agents[selectedAgent].name, auto_edit);
                printf("Auto-edit: %s\n", auto_edit ? "ON" : "OFF");
            } else if (in_prompt) {
                // Permission prompt touch zones
                if (ui_touch_yes(touch)) {
                    printf("Sending yes\n");
                    network_send_action(agents[selectedAgent].name, "yes", selectedAgent);
                } else if (ui_touch_always(touch)) {
                    printf("Sending always\n");
                    network_send_action(agents[selectedAgent].name, "always", selectedAgent);
                } else if (ui_touch_no(touch)) {
                    printf("Sending no\n");
                    network_send_action(agents[selectedAgent].name, "no", selectedAgent);
                }
            } else if (in_fight) {
                // FIGHT wheel touch zones (large hitboxes)
                int tapped_opt = ui_touch_fight_option(touch, opt_count);
                if (tapped_opt >= 0) {
                    ui_fight_set_highlight(tapped_opt);
                    printf("FIGHT touch option %d, sending pick\n", tapped_opt);
                    network_send_pick(selectedAgent, tapped_opt);
                } else if (opt_count == 0) {
                    // Touch in fight area but no options
                    printf("Touch: NO MOVES (option_count=0)\n");
                    ui_flash_no_moves();
                }
            }
        }

        // Physical buttons depend on mode
        if (in_prompt) {
            // Permission prompt mode: A=yes, B=no, X=always
            if (kDown & KEY_A) {
                printf("Button A: yes\n");
                network_send_action(agents[selectedAgent].name, "yes", selectedAgent);
            }
            if (kDown & KEY_B) {
                printf("Button B: no\n");
                network_send_action(agents[selectedAgent].name, "no", selectedAgent);
            }
            if (kDown & KEY_X) {
                printf("Button X: always\n");
                network_send_action(agents[selectedAgent].name, "always", selectedAgent);
            }
        } else if (in_fight) {
            // FIGHT wheel mode: A=send pick, B=RUN (stop)
            // Always respond to A - never silently drop
            if (kDown & KEY_A) {
                if (opt_count > 0) {
                    // Clamp highlight to valid range
                    int highlight = ui_fight_get_highlight();
                    if (highlight < 0) highlight = 0;
                    if (highlight >= opt_count) highlight = opt_count - 1;
                    ui_fight_set_highlight(highlight);
                    printf("Button A: pick option %d\n", highlight);
                    network_send_pick(selectedAgent, highlight);
                } else {
                    // No options available - show visible feedback
                    printf("Button A: NO MOVES (option_count=0)\n");
                    ui_flash_no_moves();
                }
            }
            // Always respond to B - send RUN command
            if (kDown & KEY_B) {
                printf("Button B: RUN (stop)\n");
                network_send_run(selectedAgent);
                ui_flash_run_sent();
            }
        }

        // Y = toggle auto-edit (works anytime)
        if (kDown & KEY_Y) {
            auto_edit = !auto_edit;
            ui_set_auto_edit(auto_edit);
            network_send_config(agents[selectedAgent].name, auto_edit);
            printf("Button Y: auto-edit %s\n", auto_edit ? "ON" : "OFF");
        }

        // Circle pad for navigation (debounced)
        if (scroll_cooldown > 0) scroll_cooldown--;
        circlePosition cpad;
        hidCircleRead(&cpad);
        if (scroll_cooldown == 0) {
            if (in_prompt) {
                // In prompt mode, circle pad scrolls tool detail
                if (cpad.dy > 40) {
                    ui_scroll_detail(-1);
                    scroll_cooldown = 8;
                } else if (cpad.dy < -40) {
                    ui_scroll_detail(1);
                    scroll_cooldown = 8;
                }
            } else if (in_fight && opt_count > 0) {
                // In FIGHT mode with options, circle pad navigates options
                if (cpad.dy > 40) {
                    ui_fight_highlight_up();
                    scroll_cooldown = 10;
                } else if (cpad.dy < -40) {
                    int highlight = ui_fight_get_highlight();
                    if (highlight < opt_count - 1) {
                        ui_fight_highlight_down();
                    }
                    scroll_cooldown = 10;
                }
            }
        }

        // D-pad navigation depends on mode
        if (in_fight && opt_count > 0) {
            // D-pad up/down navigates FIGHT wheel (only when options exist)
            if (kDown & KEY_UP) {
                ui_fight_highlight_up();
            }
            if (kDown & KEY_DOWN) {
                int highlight = ui_fight_get_highlight();
                if (highlight < opt_count - 1) {
                    ui_fight_highlight_down();
                }
            }
            // D-pad left/right switches ACTIVE agents in FIGHT mode
            // Check both kDown (instant) and kHeld (with cooldown) for reliability
            if (agent_count > 0 && agent_switch_cooldown == 0) {
                if ((kDown | kHeld) & KEY_LEFT) {
                    selectedAgent = find_prev_active(selectedAgent, agent_count);
                    ui_fight_set_highlight(0);
                    agent_switch_cooldown = (kDown & KEY_LEFT) ? 12 : 10;
                }
                if ((kDown | kHeld) & KEY_RIGHT) {
                    selectedAgent = find_next_active(selectedAgent, agent_count);
                    ui_fight_set_highlight(0);
                    agent_switch_cooldown = (kDown & KEY_RIGHT) ? 12 : 10;
                }
            }
        } else if (in_fight) {
            // FIGHT mode with no options - D-pad switches ACTIVE agents
            if (agent_count > 0 && agent_switch_cooldown == 0) {
                if ((kDown | kHeld) & KEY_LEFT) {
                    selectedAgent = find_prev_active(selectedAgent, agent_count);
                    agent_switch_cooldown = (kDown & KEY_LEFT) ? 12 : 10;
                }
                if ((kDown | kHeld) & KEY_RIGHT) {
                    selectedAgent = find_next_active(selectedAgent, agent_count);
                    agent_switch_cooldown = (kDown & KEY_RIGHT) ? 12 : 10;
                }
            }
        } else {
            // In prompt mode: D-pad scrolls detail
            if (kDown & KEY_LEFT) {
                ui_scroll_detail(-1);
            }
            if (kDown & KEY_RIGHT) {
                ui_scroll_detail(1);
            }
            // D-pad up/down switches ACTIVE agents
            if (kDown & KEY_DOWN && agent_count > 0) {
                selectedAgent = find_next_active(selectedAgent, agent_count);
            }
            if (kDown & KEY_UP && agent_count > 0) {
                selectedAgent = find_prev_active(selectedAgent, agent_count);
            }
        }

        // L/R bumpers cycle through ACTIVE agents only
        // Check both kDown (instant) and kHeld (with cooldown) for Azahar reliability
        if (agent_count > 0 && agent_switch_cooldown == 0) {
            if ((kDown | kHeld) & KEY_R) {
                selectedAgent = find_next_active(selectedAgent, agent_count);
                ui_fight_set_highlight(0);
                agent_switch_cooldown = (kDown & KEY_R) ? 12 : 10;
            }
            if ((kDown | kHeld) & KEY_L) {
                selectedAgent = find_prev_active(selectedAgent, agent_count);
                ui_fight_set_highlight(0);
                agent_switch_cooldown = (kDown & KEY_L) ? 12 : 10;
            }
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
