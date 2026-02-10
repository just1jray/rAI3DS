#ifndef AUDIO_H
#define AUDIO_H

// Initialize CSND audio service and pre-generate beep buffer
void audio_init(void);

// Cleanup audio resources
void audio_exit(void);

// Play a short 880Hz beep (~150ms) for permission prompt notification
// Non-blocking: fires and returns immediately
void audio_play_prompt_beep(void);

#endif // AUDIO_H
