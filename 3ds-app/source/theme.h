#ifndef THEME_H
#define THEME_H

#include <citro2d.h>
#include <stdbool.h>

typedef enum {
    THEME_MODERN = 0,      // Cleaned-up Catppuccin (dark, rounded cards)
    THEME_HOME,            // Official 3DS HOME menu (aqua/teal top bar, beige cards)
    THEME_GODMODE9,        // Terminal payload (black, bright green, sharp rectangles)
    THEME_TWILIGHT,        // Twilight Menu++ (dark purple, soft gradients)
    THEME_COUNT
} ThemeId;

typedef enum {
    CHROME_ROUNDED = 0,    // Rounded corners, soft edges
    CHROME_SHARP,          // Sharp rectangles, no rounding
    CHROME_FOLDER          // HOME-style folder cards
} ChromeStyle;

typedef struct {
    const char* name;

    // Base backgrounds
    u32 base;              // Screen background
    u32 mantle;            // Card/panel background
    u32 crust;             // Title/footer bar background

    // Surface layers
    u32 surface0;          // Disabled buttons, inactive tabs
    u32 surface1;          // Borders, separators
    u32 surface2;          // Progress bar borders

    // Text colors
    u32 overlay0;          // Dimmed/disabled text
    u32 subtext0;          // Secondary text
    u32 subtext1;          // Brighter secondary text
    u32 text;              // Primary text
    u32 textAlt;           // Text on colored backgrounds (e.g., button labels)

    // Accent colors
    u32 blue;              // Working state
    u32 green;             // Done/approve/YES
    u32 red;               // Error/deny/NO
    u32 yellow;            // Waiting state
    u32 peach;             // Tool names
    u32 mauve;             // Accents, active tab
    u32 lavender;          // Highlights, title
    u32 teal;              // Healthy context bar
    u32 sapphire;          // Info accent

    // Chrome style
    ChromeStyle chrome;

    // Option kind colors (FIGHT wheel)
    u32 optSteer;
    u32 optQuestion;
    u32 optAction;
    u32 optMeta;

    // Highlight style
    u32 highlight;         // Selected item background
    u32 highlightBorder;   // Selected item border

} Theme;

// Initialize theme system (loads saved theme from SD if available)
void theme_init(void);

// Get current theme
const Theme* theme_get(void);

// Get current theme ID
ThemeId theme_get_id(void);

// Get theme name
const char* theme_get_name(void);

// Set theme by ID
void theme_set(ThemeId id);

// Cycle to next theme
void theme_cycle(void);

// Save current theme to SD card (returns true on success)
bool theme_save(void);

// Load theme from SD card (returns true on success)
bool theme_load(void);

#endif // THEME_H
