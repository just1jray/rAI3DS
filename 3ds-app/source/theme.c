#include "theme.h"
#include <stdio.h>
#include <string.h>
#include <3ds.h>

#define THEME_CFG_PATH "sdmc:/3ds/raids/theme.cfg"

static ThemeId current_theme = THEME_MODERN;

// ============================================================================
// THEME DEFINITIONS
// ============================================================================

// Theme 1: MODERN — Cleaned-up Catppuccin Mocha
// Dark base, rounded cards, the existing rAI3DS soul
// Hex reference: https://github.com/catppuccin/catppuccin
static const Theme theme_modern = {
    .name = "Modern",
    // Base: #1e1e2e | Mantle: #181825 | Crust: #11111b
    .base     = 0xFF2e1e1e,  // ABGR: 1e1e2e
    .mantle   = 0xFF251818,
    .crust    = 0xFF1b1111,
    // Surface layers
    .surface0 = 0xFF443231,  // #313244
    .surface1 = 0xFF5a4745,  // #45475a
    .surface2 = 0xFF705b58,  // #585b70
    // Text
    .overlay0 = 0xFF86706c,  // #6c7086
    .subtext0 = 0xFFc8ada6,  // #a6adc8
    .subtext1 = 0xFFdec2ba,  // #bac2de
    .text     = 0xFFf4d6cd,  // #cdd6f4
    .textAlt  = 0xFF1b1111,  // dark text for bright buttons
    // Accents
    .blue     = 0xFFfab489,  // #89b4fa
    .green    = 0xFFa1e3a6,  // #a6e3a1
    .red      = 0xFFa88bf3,  // #f38ba8
    .yellow   = 0xFFafe2f9,  // #f9e2af
    .peach    = 0xFF87b3fa,  // #fab387
    .mauve    = 0xFFf7a6cb,  // #cba6f7
    .lavender = 0xFFfebeb4,  // #b4befe
    .teal     = 0xFFd5e294,  // #94e2d5
    .sapphire = 0xFFec74c7,  // #74c7ec (Note: ABGR format)
    // Chrome
    .chrome   = CHROME_ROUNDED,
    // Option kinds
    .optSteer    = 0xFFfab489,
    .optQuestion = 0xFFf7a6cb,
    .optAction   = 0xFFa1e3a6,
    .optMeta     = 0xFF87b3fa,
    // Highlight
    .highlight       = 0xFF5a4745,
    .highlightBorder = 0xFFf7a6cb,
};

// Theme 2: HOME — Official 3DS HOME Menu
// Aqua/teal top bar, pale beige/white cards, friendly system UI
// Folder-row energy, light mode
static const Theme theme_home = {
    .name = "HOME",
    // Light beige base, white cards, teal accent bar
    .base     = 0xFFe8e0d8,  // #d8e0e8 pale gray-beige
    .mantle   = 0xFFffffff,  // #ffffff white cards
    .crust    = 0xFFb8a858,  // #58a8b8 teal/aqua top bar
    // Surface layers
    .surface0 = 0xFFd8d0c8,  // #c8d0d8 light gray
    .surface1 = 0xFFc8c0b8,  // #b8c0c8 border gray
    .surface2 = 0xFFa8a098,  // #98a0a8 darker border
    // Text
    .overlay0 = 0xFF888080,  // #808088 dimmed
    .subtext0 = 0xFF686060,  // #606068 secondary
    .subtext1 = 0xFF484040,  // #404048 brighter secondary
    .text     = 0xFF282020,  // #202028 primary (near black)
    .textAlt  = 0xFFffffff,  // white text on colored buttons
    // Accents — friendly, system-like
    .blue     = 0xFFd8a048,  // #48a0d8 sky blue
    .green    = 0xFF78c878,  // #78c878 grass green
    .red      = 0xFF5858d8,  // #d85858 soft red
    .yellow   = 0xFF58c8e8,  // #e8c858 warm yellow
    .peach    = 0xFF78a8d8,  // #d8a878 soft orange
    .mauve    = 0xFFd888a8,  // #a888d8 light purple
    .lavender = 0xFFe8a888,  // #88a8e8 periwinkle
    .teal     = 0xFFc8b858,  // #58b8c8 system teal
    .sapphire = 0xFFe8a878,  // #78a8e8 info blue
    // Chrome
    .chrome   = CHROME_FOLDER,
    // Option kinds — HOME style
    .optSteer    = 0xFFd8a048,
    .optQuestion = 0xFFd888a8,
    .optAction   = 0xFF78c878,
    .optMeta     = 0xFF78a8d8,
    // Highlight
    .highlight       = 0xFFf0e8e0,  // Slightly darker than white
    .highlightBorder = 0xFFc8b858,  // Teal border
};

// Theme 3: GODMODE9 — Luma3DS Payload Menu Style
// Black terminal, bright green/yellow monospace, sharp rectangles
// File manager payload energy
static const Theme theme_godmode9 = {
    .name = "GodMode9",
    // Pure black base, sharp terminal style
    .base     = 0xFF000000,  // #000000 pure black
    .mantle   = 0xFF101010,  // #101010 dark gray panels
    .crust    = 0xFF000000,  // #000000 black bars
    // Surface layers — minimal, dark
    .surface0 = 0xFF181818,  // #181818
    .surface1 = 0xFF282828,  // #282828
    .surface2 = 0xFF383838,  // #383838
    // Text — terminal greens
    .overlay0 = 0xFF308830,  // #308830 dim green
    .subtext0 = 0xFF40b040,  // #40b040 secondary green
    .subtext1 = 0xFF50d050,  // #50d050 brighter green
    .text     = 0xFF60ff60,  // #60ff60 bright terminal green
    .textAlt  = 0xFF000000,  // black text on bright
    // Accents — Luma yellow-green palette
    .blue     = 0xFF60d0ff,  // #ffd060 yellow (info)
    .green    = 0xFF60ff60,  // #60ff60 green (success)
    .red      = 0xFF6060ff,  // #ff6060 red (error)
    .yellow   = 0xFF60e0ff,  // #ffe060 bright yellow
    .peach    = 0xFF60c0ff,  // #ffc060 orange
    .mauve    = 0xFFff60c0,  // #c060ff purple
    .lavender = 0xFFff80ff,  // #ff80ff magenta
    .teal     = 0xFFffff60,  // #60ffff cyan
    .sapphire = 0xFFffa060,  // #60a0ff light blue
    // Chrome
    .chrome   = CHROME_SHARP,
    // Option kinds — terminal colors
    .optSteer    = 0xFFffff60,  // Cyan
    .optQuestion = 0xFFff60c0,  // Purple
    .optAction   = 0xFF60ff60,  // Green
    .optMeta     = 0xFF60d0ff,  // Yellow
    // Highlight
    .highlight       = 0xFF003000,  // Dark green background
    .highlightBorder = 0xFF60ff60,  // Bright green border
};

// Theme 4: TWILIGHT — Twilight Menu++ Dark Purple
// Deep purple base, soft violet accents, elegant dark theme
static const Theme theme_twilight = {
    .name = "Twilight",
    // Deep purple base
    .base     = 0xFF281828,  // #281828 dark purple-black
    .mantle   = 0xFF382038,  // #382038 purple panel
    .crust    = 0xFF180818,  // #180818 near-black purple
    // Surface layers
    .surface0 = 0xFF402840,  // #402840
    .surface1 = 0xFF503850,  // #503850
    .surface2 = 0xFF604860,  // #604860
    // Text — soft white/lavender
    .overlay0 = 0xFF9070a0,  // #a07090 dim lavender
    .subtext0 = 0xFFb090c0,  // #c090b0 secondary
    .subtext1 = 0xFFd0b0e0,  // #e0b0d0 brighter
    .text     = 0xFFf0d8f8,  // #f8d8f0 soft white-pink
    .textAlt  = 0xFF180818,  // dark text
    // Accents — purple/violet palette
    .blue     = 0xFFe0a0d0,  // #d0a0e0 violet-blue
    .green    = 0xFF90e0a0,  // #a0e090 mint green
    .red      = 0xFF8080f0,  // #f08080 soft red
    .yellow   = 0xFFa0e8f8,  // #f8e8a0 cream yellow
    .peach    = 0xFFA0c0f0,  // #f0c0a0 peach
    .mauve    = 0xFFf080d0,  // #d080f0 bright purple
    .lavender = 0xFFf0b0e0,  // #e0b0f0 lavender
    .teal     = 0xFFe0c090,  // #90c0e0 soft cyan
    .sapphire = 0xFFf0a0c0,  // #c0a0f0 periwinkle
    // Chrome
    .chrome   = CHROME_ROUNDED,
    // Option kinds — twilight purple
    .optSteer    = 0xFFe0a0d0,
    .optQuestion = 0xFFf080d0,
    .optAction   = 0xFF90e0a0,
    .optMeta     = 0xFFa0e8f8,
    // Highlight
    .highlight       = 0xFF503850,
    .highlightBorder = 0xFFf080d0,
};

// Theme lookup table
static const Theme* themes[THEME_COUNT] = {
    &theme_modern,
    &theme_home,
    &theme_godmode9,
    &theme_twilight,
};

// ============================================================================
// THEME FUNCTIONS
// ============================================================================

void theme_init(void) {
    current_theme = THEME_MODERN;
    theme_load();  // Try to load saved preference
}

const Theme* theme_get(void) {
    return themes[current_theme];
}

ThemeId theme_get_id(void) {
    return current_theme;
}

const char* theme_get_name(void) {
    return themes[current_theme]->name;
}

void theme_set(ThemeId id) {
    if (id >= THEME_COUNT) id = THEME_MODERN;
    current_theme = id;
}

void theme_cycle(void) {
    current_theme = (current_theme + 1) % THEME_COUNT;
    theme_save();  // Auto-save on cycle
}

bool theme_save(void) {
    // Create directory if needed
    Result rc = FSUSER_CreateDirectory(0, fsMakePath(PATH_ASCII, "/3ds"), FS_ATTRIBUTE_DIRECTORY);
    (void)rc;  // Ignore error if exists
    rc = FSUSER_CreateDirectory(0, fsMakePath(PATH_ASCII, "/3ds/raids"), FS_ATTRIBUTE_DIRECTORY);
    (void)rc;

    FILE* f = fopen(THEME_CFG_PATH, "w");
    if (!f) {
        printf("Failed to save theme config\n");
        return false;
    }

    fprintf(f, "%d\n", (int)current_theme);
    fclose(f);
    printf("Theme saved: %s\n", themes[current_theme]->name);
    return true;
}

bool theme_load(void) {
    FILE* f = fopen(THEME_CFG_PATH, "r");
    if (!f) {
        return false;
    }

    int id = 0;
    if (fscanf(f, "%d", &id) == 1) {
        if (id >= 0 && id < THEME_COUNT) {
            current_theme = (ThemeId)id;
            printf("Theme loaded: %s\n", themes[current_theme]->name);
            fclose(f);
            return true;
        }
    }

    fclose(f);
    return false;
}
