#include "creature.h"
#include <citro2d.h>

// Catppuccin Mocha palette colors for Clawd
#define CLR_TRANSPARENT 0x00000000
#define CLR_BODY    0xFFFA87B3  // Peach #fab387 (ABGR for citro2d)
#define CLR_DARK    0xFF1B1111  // Crust #11111b — eyes, outline
#define CLR_EYE     0xFF2E1E1E  // Base  #1e1e2e — eye cutouts
#define CLR_LIGHT   0xFFECCEAD  // lighter peach for belly/highlights
#define CLR_CLAW    0xFFF78BA6  // Mauve-ish for claw tips
#define CLR_ANTENNA 0xFFF7A6CB  // Mauve #cba6f7

// Shorthand
#define __ CLR_TRANSPARENT
#define BB CLR_BODY
#define DD CLR_DARK
#define EE CLR_EYE
#define LL CLR_LIGHT
#define CC CLR_CLAW
#define AA CLR_ANTENNA

// Clawd frame 0: normal pose
// Crab-like creature inspired by Claude's TUI crab
static const CreatureFrame clawd_frame0 = { .pixels = {
//   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
    {__, __, __, AA, __, __, __, __, __, __, __, __, AA, __, __, __},  // 0: antenna tips
    {__, __, __, DD, AA, __, __, __, __, __, __, AA, DD, __, __, __},  // 1: antenna stalks
    {__, __, __, __, DD, __, __, __, __, __, __, DD, __, __, __, __},  // 2: antenna base
    {__, __, __, DD, DD, DD, DD, DD, DD, DD, DD, DD, DD, __, __, __},  // 3: body top border
    {__, __, DD, BB, BB, EE, EE, BB, BB, EE, EE, BB, BB, DD, __, __},  // 4: body with eyes
    {__, CC, DD, BB, BB, EE, EE, BB, BB, EE, EE, BB, BB, DD, CC, __},  // 5: body + arm nubs
    {__, CC, DD, BB, BB, BB, LL, LL, LL, LL, BB, BB, BB, DD, CC, __},  // 6: body belly
    {__, __, DD, BB, BB, BB, LL, LL, LL, LL, BB, BB, BB, DD, __, __},  // 7: body belly
    {__, __, DD, BB, BB, BB, BB, BB, BB, BB, BB, BB, BB, DD, __, __},  // 8: body lower
    {__, __, __, DD, DD, DD, DD, DD, DD, DD, DD, DD, DD, __, __, __},  // 9: body bottom border
    {__, __, DD, DD, __, __, __, __, __, __, __, __, DD, DD, __, __},  // 10: upper legs
    {__, DD, DD, __, __, __, __, __, __, __, __, __, __, DD, DD, __},  // 11: legs spread
    {__, DD, __, __, __, DD, DD, __, __, DD, DD, __, __, __, DD, __},  // 12: legs + inner legs
    {DD, DD, __, __, DD, DD, __, __, __, __, DD, DD, __, __, DD, DD},  // 13: feet spreading
    {CC, __, __, __, CC, __, __, __, __, __, __, CC, __, __, __, CC},  // 14: claw feet
    {__, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __},  // 15: empty
}};

// Clawd frame 1: raised 1px (bob animation — shift body up 1 row)
static const CreatureFrame clawd_frame1 = { .pixels = {
    {__, __, __, AA, __, __, __, __, __, __, __, __, AA, __, __, __},  // 0: antenna (was row -1, clip)
    {__, __, __, __, DD, __, __, __, __, __, __, DD, __, __, __, __},  // 1
    {__, __, __, DD, DD, DD, DD, DD, DD, DD, DD, DD, DD, __, __, __},  // 2
    {__, __, DD, BB, BB, EE, EE, BB, BB, EE, EE, BB, BB, DD, __, __},  // 3
    {__, CC, DD, BB, BB, EE, EE, BB, BB, EE, EE, BB, BB, DD, CC, __},  // 4
    {__, CC, DD, BB, BB, BB, LL, LL, LL, LL, BB, BB, BB, DD, CC, __},  // 5
    {__, __, DD, BB, BB, BB, LL, LL, LL, LL, BB, BB, BB, DD, __, __},  // 6
    {__, __, DD, BB, BB, BB, BB, BB, BB, BB, BB, BB, BB, DD, __, __},  // 7
    {__, __, __, DD, DD, DD, DD, DD, DD, DD, DD, DD, DD, __, __, __},  // 8
    {__, __, DD, DD, __, __, __, __, __, __, __, __, DD, DD, __, __},  // 9
    {__, DD, DD, __, __, __, __, __, __, __, __, __, __, DD, DD, __},  // 10
    {__, DD, __, __, __, DD, DD, __, __, DD, DD, __, __, __, DD, __},  // 11
    {DD, DD, __, __, DD, DD, __, __, __, __, DD, DD, __, __, DD, DD},  // 12
    {CC, __, __, __, CC, __, __, __, __, __, __, CC, __, __, __, CC},  // 13
    {__, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __},  // 14
    {__, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __},  // 15
}};


const CreatureFrame* creature_get_clawd_frame(int frame_index) {
    if (frame_index <= 0) return &clawd_frame0;
    return &clawd_frame1;
}

void draw_creature(float x, float y, int scale, const CreatureFrame* frame) {
    if (!frame) return;

    for (int row = 0; row < CREATURE_SIZE_H; row++) {
        for (int col = 0; col < CREATURE_W; col++) {
            u32 color = frame->pixels[row][col];
            if (color == CLR_TRANSPARENT) continue;

            float px = x + col * scale;
            float py = y + row * scale;
            C2D_DrawRectSolid(px, py, 0, (float)scale, (float)scale, color);
        }
    }
}
