#include "spritegen.h"
#include <string.h>

// NES-style limited palette (base colors, will be tinted per-seed)
// Colors are ABGR for citro2d
#define CLR_TRANSPARENT 0x00000000

// Simple PRNG for deterministic generation from seed
static uint32_t prng_state;

static void prng_seed(uint32_t seed) {
    prng_state = seed;
}

static uint32_t prng_next(void) {
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 17;
    prng_state ^= prng_state << 5;
    return prng_state;
}

static uint8_t prng_range(uint8_t min, uint8_t max) {
    return min + (prng_next() % (max - min + 1));
}

// Generate a color in ABGR format
static uint32_t make_color(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000 | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

// Generate palette from seed
typedef struct {
    uint32_t body;
    uint32_t dark;      // outline/eyes
    uint32_t light;     // highlights/belly
    uint32_t accent;    // details (claws, antenna, etc)
    uint32_t eye;       // eye color (usually dark or accent)
} SpritePalette;

static void generate_palette(SpritePalette* pal, uint32_t seed) {
    prng_seed(seed * 31337);
    
    // Pick a base hue (0-5 for different color families)
    int hue = prng_next() % 6;
    
    uint8_t r, g, b;
    switch (hue) {
        case 0: // Red/pink family
            r = prng_range(180, 255); g = prng_range(80, 140); b = prng_range(100, 160);
            break;
        case 1: // Orange/peach family
            r = prng_range(200, 255); g = prng_range(140, 200); b = prng_range(80, 130);
            break;
        case 2: // Green/teal family
            r = prng_range(80, 150); g = prng_range(180, 240); b = prng_range(140, 200);
            break;
        case 3: // Blue/cyan family
            r = prng_range(100, 160); g = prng_range(160, 220); b = prng_range(200, 255);
            break;
        case 4: // Purple/mauve family
            r = prng_range(160, 220); g = prng_range(100, 160); b = prng_range(200, 255);
            break;
        default: // Yellow/gold family
            r = prng_range(220, 255); g = prng_range(200, 240); b = prng_range(100, 160);
            break;
    }
    
    pal->body = make_color(r, g, b);
    
    // Light = brighter version
    uint8_t lr = (r + 255) / 2;
    uint8_t lg = (g + 255) / 2;
    uint8_t lb = (b + 255) / 2;
    pal->light = make_color(lr, lg, lb);
    
    // Dark = dark outline (nearly black with slight tint)
    pal->dark = make_color(0x15 + (r >> 4), 0x15 + (g >> 4), 0x20 + (b >> 4));
    
    // Eye = slightly lighter than dark
    pal->eye = make_color(0x25 + (r >> 4), 0x25 + (g >> 4), 0x30 + (b >> 4));
    
    // Accent = complementary-ish color
    pal->accent = make_color(
        (255 - r/2 + prng_range(0, 50)) & 0xFF,
        (255 - g/2 + prng_range(0, 50)) & 0xFF,
        b
    );
}

// Copy row with 1px vertical offset (for bob animation)
static void copy_row_offset(CreatureFrame* dest, const CreatureFrame* src, int dy) {
    memset(dest->pixels, 0, sizeof(dest->pixels));
    for (int row = 0; row < CREATURE_SIZE_H; row++) {
        int src_row = row - dy;
        if (src_row >= 0 && src_row < CREATURE_SIZE_H) {
            memcpy(dest->pixels[row], src->pixels[src_row], sizeof(dest->pixels[row]));
        }
    }
}

// ============ ARCHETYPE: CRAB ============
static void gen_crab(CreatureFrame* f, const SpritePalette* p) {
    uint32_t BB = p->body, DD = p->dark, LL = p->light, AA = p->accent, EE = p->eye;
    uint32_t __ = CLR_TRANSPARENT;
    
    // Row 0-2: antenna
    f->pixels[0][3] = AA; f->pixels[0][12] = AA;
    f->pixels[1][3] = DD; f->pixels[1][4] = AA; f->pixels[1][11] = AA; f->pixels[1][12] = DD;
    f->pixels[2][4] = DD; f->pixels[2][11] = DD;
    
    // Row 3: body top border
    for (int c = 3; c <= 12; c++) f->pixels[3][c] = DD;
    
    // Row 4-5: body with eyes
    f->pixels[4][2] = DD;
    for (int c = 3; c <= 5; c++) f->pixels[4][c] = BB;
    f->pixels[4][5] = EE; f->pixels[4][6] = EE;
    for (int c = 7; c <= 8; c++) f->pixels[4][c] = BB;
    f->pixels[4][9] = EE; f->pixels[4][10] = EE;
    for (int c = 10; c <= 12; c++) f->pixels[4][c] = BB;
    f->pixels[4][13] = DD;
    
    f->pixels[5][1] = AA; f->pixels[5][2] = DD;
    for (int c = 3; c <= 5; c++) f->pixels[5][c] = BB;
    f->pixels[5][5] = EE; f->pixels[5][6] = EE;
    for (int c = 7; c <= 8; c++) f->pixels[5][c] = BB;
    f->pixels[5][9] = EE; f->pixels[5][10] = EE;
    for (int c = 10; c <= 12; c++) f->pixels[5][c] = BB;
    f->pixels[5][13] = DD; f->pixels[5][14] = AA;
    
    // Row 6-7: body belly
    f->pixels[6][1] = AA; f->pixels[6][2] = DD;
    for (int c = 3; c <= 5; c++) f->pixels[6][c] = BB;
    for (int c = 6; c <= 9; c++) f->pixels[6][c] = LL;
    for (int c = 10; c <= 12; c++) f->pixels[6][c] = BB;
    f->pixels[6][13] = DD; f->pixels[6][14] = AA;
    
    f->pixels[7][2] = DD;
    for (int c = 3; c <= 5; c++) f->pixels[7][c] = BB;
    for (int c = 6; c <= 9; c++) f->pixels[7][c] = LL;
    for (int c = 10; c <= 12; c++) f->pixels[7][c] = BB;
    f->pixels[7][13] = DD;
    
    // Row 8: body lower
    f->pixels[8][2] = DD;
    for (int c = 3; c <= 12; c++) f->pixels[8][c] = BB;
    f->pixels[8][13] = DD;
    
    // Row 9: body bottom border
    for (int c = 3; c <= 12; c++) f->pixels[9][c] = DD;
    
    // Row 10-14: legs
    f->pixels[10][2] = DD; f->pixels[10][3] = DD; f->pixels[10][12] = DD; f->pixels[10][13] = DD;
    f->pixels[11][1] = DD; f->pixels[11][2] = DD; f->pixels[11][13] = DD; f->pixels[11][14] = DD;
    f->pixels[12][1] = DD; f->pixels[12][5] = DD; f->pixels[12][6] = DD;
    f->pixels[12][9] = DD; f->pixels[12][10] = DD; f->pixels[12][14] = DD;
    f->pixels[13][0] = DD; f->pixels[13][1] = DD; f->pixels[13][4] = DD; f->pixels[13][5] = DD;
    f->pixels[13][10] = DD; f->pixels[13][11] = DD; f->pixels[13][14] = DD; f->pixels[13][15] = DD;
    f->pixels[14][0] = AA; f->pixels[14][4] = AA; f->pixels[14][11] = AA; f->pixels[14][15] = AA;
}

// ============ ARCHETYPE: BLOB ============
static void gen_blob(CreatureFrame* f, const SpritePalette* p) {
    uint32_t BB = p->body, DD = p->dark, LL = p->light, EE = p->eye;
    
    // Rounded blob shape
    // Row 2-3: top
    for (int c = 5; c <= 10; c++) f->pixels[2][c] = DD;
    for (int c = 4; c <= 11; c++) f->pixels[3][c] = DD;
    f->pixels[3][5] = BB; f->pixels[3][6] = BB; f->pixels[3][9] = BB; f->pixels[3][10] = BB;
    
    // Row 4-8: body
    for (int row = 4; row <= 8; row++) {
        f->pixels[row][3] = DD;
        for (int c = 4; c <= 11; c++) f->pixels[row][c] = BB;
        f->pixels[row][12] = DD;
    }
    
    // Eyes (row 5-6)
    f->pixels[5][5] = EE; f->pixels[5][6] = EE;
    f->pixels[5][9] = EE; f->pixels[5][10] = EE;
    f->pixels[6][5] = EE; f->pixels[6][6] = EE;
    f->pixels[6][9] = EE; f->pixels[6][10] = EE;
    
    // Belly highlight
    for (int c = 6; c <= 9; c++) {
        f->pixels[7][c] = LL;
        f->pixels[8][c] = LL;
    }
    
    // Row 9-10: bottom curve
    f->pixels[9][3] = DD;
    for (int c = 4; c <= 11; c++) f->pixels[9][c] = BB;
    f->pixels[9][12] = DD;
    
    for (int c = 4; c <= 11; c++) f->pixels[10][c] = DD;
    
    // Row 11-12: little feet
    f->pixels[11][5] = DD; f->pixels[11][6] = DD;
    f->pixels[11][9] = DD; f->pixels[11][10] = DD;
    f->pixels[12][5] = BB; f->pixels[12][6] = BB;
    f->pixels[12][9] = BB; f->pixels[12][10] = BB;
}

// ============ ARCHETYPE: BOT ============
static void gen_bot(CreatureFrame* f, const SpritePalette* p) {
    uint32_t BB = p->body, DD = p->dark, LL = p->light, AA = p->accent, EE = p->eye;
    
    // Antenna
    f->pixels[0][7] = AA; f->pixels[0][8] = AA;
    f->pixels[1][7] = DD; f->pixels[1][8] = DD;
    
    // Head (boxy)
    for (int c = 4; c <= 11; c++) f->pixels[2][c] = DD;
    for (int row = 3; row <= 6; row++) {
        f->pixels[row][4] = DD;
        for (int c = 5; c <= 10; c++) f->pixels[row][c] = BB;
        f->pixels[row][11] = DD;
    }
    
    // Eyes (LED style)
    f->pixels[4][5] = EE; f->pixels[4][6] = AA;
    f->pixels[4][9] = AA; f->pixels[4][10] = EE;
    f->pixels[5][5] = EE; f->pixels[5][6] = AA;
    f->pixels[5][9] = AA; f->pixels[5][10] = EE;
    
    // Head bottom
    for (int c = 4; c <= 11; c++) f->pixels[7][c] = DD;
    
    // Neck
    f->pixels[8][6] = DD; f->pixels[8][7] = BB; f->pixels[8][8] = BB; f->pixels[8][9] = DD;
    
    // Body (wider, boxy)
    for (int c = 3; c <= 12; c++) f->pixels[9][c] = DD;
    for (int row = 10; row <= 12; row++) {
        f->pixels[row][3] = DD;
        for (int c = 4; c <= 11; c++) f->pixels[row][c] = BB;
        f->pixels[row][12] = DD;
    }
    
    // Chest panel
    for (int c = 6; c <= 9; c++) f->pixels[10][c] = LL;
    f->pixels[11][7] = AA; f->pixels[11][8] = AA;
    
    // Body bottom
    for (int c = 3; c <= 12; c++) f->pixels[13][c] = DD;
    
    // Legs
    f->pixels[14][4] = DD; f->pixels[14][5] = BB; f->pixels[14][6] = DD;
    f->pixels[14][9] = DD; f->pixels[14][10] = BB; f->pixels[14][11] = DD;
    f->pixels[15][4] = DD; f->pixels[15][5] = DD; f->pixels[15][6] = DD;
    f->pixels[15][9] = DD; f->pixels[15][10] = DD; f->pixels[15][11] = DD;
}

// ============ ARCHETYPE: BIRD ============
static void gen_bird(CreatureFrame* f, const SpritePalette* p) {
    uint32_t BB = p->body, DD = p->dark, LL = p->light, AA = p->accent, EE = p->eye;
    
    // Crest
    f->pixels[0][6] = AA; f->pixels[0][7] = AA;
    f->pixels[1][5] = AA; f->pixels[1][6] = DD; f->pixels[1][7] = DD; f->pixels[1][8] = AA;
    
    // Head
    for (int c = 5; c <= 10; c++) f->pixels[2][c] = DD;
    for (int row = 3; row <= 5; row++) {
        f->pixels[row][4] = DD;
        for (int c = 5; c <= 10; c++) f->pixels[row][c] = BB;
        f->pixels[row][11] = DD;
    }
    
    // Eyes
    f->pixels[4][6] = EE; f->pixels[4][9] = EE;
    
    // Beak
    f->pixels[5][11] = AA; f->pixels[5][12] = AA;
    f->pixels[6][11] = AA;
    
    // Head bottom
    for (int c = 5; c <= 10; c++) f->pixels[6][c] = DD;
    
    // Body
    for (int c = 4; c <= 11; c++) f->pixels[7][c] = DD;
    for (int row = 8; row <= 10; row++) {
        f->pixels[row][3] = DD;
        for (int c = 4; c <= 11; c++) f->pixels[row][c] = BB;
        f->pixels[row][12] = DD;
    }
    
    // Belly
    for (int c = 6; c <= 9; c++) {
        f->pixels[9][c] = LL;
        f->pixels[10][c] = LL;
    }
    
    // Wings (folded)
    f->pixels[8][2] = AA; f->pixels[8][3] = DD;
    f->pixels[9][1] = AA; f->pixels[9][2] = DD;
    f->pixels[8][12] = DD; f->pixels[8][13] = AA;
    f->pixels[9][13] = DD; f->pixels[9][14] = AA;
    
    // Body bottom
    for (int c = 4; c <= 11; c++) f->pixels[11][c] = DD;
    
    // Legs
    f->pixels[12][5] = DD; f->pixels[12][6] = DD;
    f->pixels[12][9] = DD; f->pixels[12][10] = DD;
    f->pixels[13][5] = AA; f->pixels[13][6] = AA;
    f->pixels[13][9] = AA; f->pixels[13][10] = AA;
    f->pixels[14][4] = AA; f->pixels[14][5] = AA; f->pixels[14][6] = AA;
    f->pixels[14][9] = AA; f->pixels[14][10] = AA; f->pixels[14][11] = AA;
}

// ============ ARCHETYPE: SLIME ============
static void gen_slime(CreatureFrame* f, const SpritePalette* p) {
    uint32_t BB = p->body, DD = p->dark, LL = p->light, EE = p->eye;
    
    // Droopy top
    f->pixels[3][7] = DD; f->pixels[3][8] = DD;
    f->pixels[4][6] = DD; f->pixels[4][7] = BB; f->pixels[4][8] = BB; f->pixels[4][9] = DD;
    
    // Body expands
    for (int c = 5; c <= 10; c++) f->pixels[5][c] = DD;
    f->pixels[5][6] = BB; f->pixels[5][7] = BB; f->pixels[5][8] = BB; f->pixels[5][9] = BB;
    
    for (int row = 6; row <= 9; row++) {
        f->pixels[row][4] = DD;
        for (int c = 5; c <= 10; c++) f->pixels[row][c] = BB;
        f->pixels[row][11] = DD;
    }
    
    // Eyes (droopy)
    f->pixels[7][5] = EE; f->pixels[7][6] = EE;
    f->pixels[7][9] = EE; f->pixels[7][10] = EE;
    f->pixels[8][6] = EE; f->pixels[8][9] = EE;
    
    // Shine
    f->pixels[6][6] = LL; f->pixels[6][7] = LL;
    
    // Bottom (spreads out like puddle)
    f->pixels[10][3] = DD;
    for (int c = 4; c <= 11; c++) f->pixels[10][c] = BB;
    f->pixels[10][12] = DD;
    
    f->pixels[11][2] = DD;
    for (int c = 3; c <= 12; c++) f->pixels[11][c] = BB;
    f->pixels[11][13] = DD;
    
    // Drip edges
    for (int c = 2; c <= 13; c++) f->pixels[12][c] = DD;
    
    // Little drips
    f->pixels[13][4] = DD; f->pixels[13][7] = DD; f->pixels[13][11] = DD;
    f->pixels[14][7] = DD;
}

// ============ ARCHETYPE: KNIGHT ============
static void gen_knight(CreatureFrame* f, const SpritePalette* p) {
    uint32_t BB = p->body, DD = p->dark, LL = p->light, AA = p->accent, EE = p->eye;
    
    // Helmet plume
    f->pixels[0][7] = AA; f->pixels[0][8] = AA;
    f->pixels[1][6] = AA; f->pixels[1][7] = AA; f->pixels[1][8] = AA; f->pixels[1][9] = AA;
    
    // Helmet top
    for (int c = 5; c <= 10; c++) f->pixels[2][c] = DD;
    
    // Helmet sides
    for (int row = 3; row <= 5; row++) {
        f->pixels[row][4] = DD;
        for (int c = 5; c <= 10; c++) f->pixels[row][c] = BB;
        f->pixels[row][11] = DD;
    }
    
    // Visor slit (eyes)
    for (int c = 5; c <= 10; c++) f->pixels[4][c] = DD;
    f->pixels[4][6] = EE; f->pixels[4][9] = EE;
    
    // Helmet bottom
    for (int c = 5; c <= 10; c++) f->pixels[6][c] = DD;
    
    // Neck/gorget
    f->pixels[7][6] = DD; f->pixels[7][7] = LL; f->pixels[7][8] = LL; f->pixels[7][9] = DD;
    
    // Chest armor
    for (int c = 4; c <= 11; c++) f->pixels[8][c] = DD;
    for (int row = 9; row <= 11; row++) {
        f->pixels[row][3] = DD;
        for (int c = 4; c <= 11; c++) f->pixels[row][c] = BB;
        f->pixels[row][12] = DD;
    }
    
    // Chest emblem
    f->pixels[9][7] = AA; f->pixels[9][8] = AA;
    f->pixels[10][6] = AA; f->pixels[10][7] = LL; f->pixels[10][8] = LL; f->pixels[10][9] = AA;
    f->pixels[11][7] = AA; f->pixels[11][8] = AA;
    
    // Armor bottom
    for (int c = 3; c <= 12; c++) f->pixels[12][c] = DD;
    
    // Legs (armored)
    f->pixels[13][4] = DD; f->pixels[13][5] = BB; f->pixels[13][6] = DD;
    f->pixels[13][9] = DD; f->pixels[13][10] = BB; f->pixels[13][11] = DD;
    f->pixels[14][4] = DD; f->pixels[14][5] = DD; f->pixels[14][6] = DD;
    f->pixels[14][9] = DD; f->pixels[14][10] = DD; f->pixels[14][11] = DD;
    f->pixels[15][4] = AA; f->pixels[15][5] = AA; f->pixels[15][6] = AA;
    f->pixels[15][9] = AA; f->pixels[15][10] = AA; f->pixels[15][11] = AA;
}

// ============ PUBLIC API ============

uint32_t spritegen_hash_name(const char* name) {
    if (!name || !name[0]) return 0;
    
    // djb2 hash
    uint32_t hash = 5381;
    while (*name) {
        hash = ((hash << 5) + hash) + (uint8_t)*name;
        name++;
    }
    return hash;
}

uint32_t spritegen_make_seed(int slot_index, const char* agent_name) {
    uint32_t name_hash = spritegen_hash_name(agent_name);
    // Combine slot with name hash to ensure different slots with same name differ
    return (name_hash ^ (slot_index * 0x9E3779B9)) + slot_index;
}

void spritegen_create(GeneratedSprite* out, uint32_t seed) {
    if (!out) return;
    
    memset(out, 0, sizeof(*out));
    out->seed = seed;
    
    // Pick archetype from seed
    out->archetype = (SpriteArchetype)(seed % ARCHETYPE_COUNT);
    
    // Generate palette
    SpritePalette pal;
    generate_palette(&pal, seed);
    
    // Generate base frame
    switch (out->archetype) {
        case ARCHETYPE_CRAB:   gen_crab(&out->frame0, &pal); break;
        case ARCHETYPE_BLOB:   gen_blob(&out->frame0, &pal); break;
        case ARCHETYPE_BOT:    gen_bot(&out->frame0, &pal); break;
        case ARCHETYPE_BIRD:   gen_bird(&out->frame0, &pal); break;
        case ARCHETYPE_SLIME:  gen_slime(&out->frame0, &pal); break;
        case ARCHETYPE_KNIGHT: gen_knight(&out->frame0, &pal); break;
        default:               gen_crab(&out->frame0, &pal); break;
    }
    
    // Generate frame1 as 1px bob (shift up)
    copy_row_offset(&out->frame1, &out->frame0, -1);
}

const CreatureFrame* spritegen_get_frame(const GeneratedSprite* sprite, int frame_index) {
    if (!sprite) return NULL;
    return (frame_index <= 0) ? &sprite->frame0 : &sprite->frame1;
}

void spritegen_create_empty(CreatureFrame* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    
    // Faint silhouette: just a dim outline of a generic shape
    uint32_t dim = 0x40313244;  // Very transparent surface color
    
    // Simple humanoid silhouette outline
    for (int c = 6; c <= 9; c++) out->pixels[3][c] = dim;
    out->pixels[4][5] = dim; out->pixels[4][10] = dim;
    for (int row = 5; row <= 8; row++) {
        out->pixels[row][5] = dim;
        out->pixels[row][10] = dim;
    }
    for (int c = 5; c <= 10; c++) out->pixels[9][c] = dim;
    out->pixels[10][6] = dim; out->pixels[10][9] = dim;
    out->pixels[11][6] = dim; out->pixels[11][9] = dim;
    out->pixels[12][5] = dim; out->pixels[12][6] = dim;
    out->pixels[12][9] = dim; out->pixels[12][10] = dim;
}
