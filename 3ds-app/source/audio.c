#include "audio.h"
#include <3ds.h>
#include <string.h>
#include <math.h>

#define SAMPLE_RATE 22050
#define BEEP_FREQ   880       // A5
#define BEEP_MS     150
#define NUM_SAMPLES ((SAMPLE_RATE * BEEP_MS) / 1000)

static s16* beep_buffer = NULL;
static bool csnd_initialized = false;

void audio_init(void) {
    if (csndInit() != 0) {
        csnd_initialized = false;
        return;
    }
    csnd_initialized = true;

    // Allocate physically contiguous memory required by CSND
    beep_buffer = (s16*)linearAlloc(NUM_SAMPLES * sizeof(s16));
    if (!beep_buffer) return;

    // Pre-generate 880Hz sine wave with fade-out envelope
    for (int i = 0; i < NUM_SAMPLES; i++) {
        float t = (float)i / SAMPLE_RATE;
        float envelope = 1.0f - ((float)i / NUM_SAMPLES);  // linear fade-out
        float sample = sinf(2.0f * M_PI * BEEP_FREQ * t) * envelope;
        beep_buffer[i] = (s16)(sample * 32000);
    }
}

void audio_exit(void) {
    if (beep_buffer) {
        linearFree(beep_buffer);
        beep_buffer = NULL;
    }
    if (csnd_initialized) {
        csndExit();
        csnd_initialized = false;
    }
}

void audio_play_prompt_beep(void) {
    if (!csnd_initialized || !beep_buffer) return;

    u32 size = NUM_SAMPLES * sizeof(s16);

    // Channel 8, 16-bit mono, no loop
    csndPlaySound(8,
        SOUND_FORMAT_16BIT | SOUND_ONE_SHOT,
        SAMPLE_RATE,
        1.0f,   // volume
        0.0f,   // pan (center)
        (u32*)beep_buffer,
        (u32*)beep_buffer,
        size);

    CSND_FlushDataCache(beep_buffer, size);
    csndExecCmds(true);
}
