#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

typedef struct {
    int octets[4];
    int port;
} AppSettings;

// Load settings from SD card. Returns true if file existed.
// Falls back to config.h defaults if missing.
bool settings_load(AppSettings* s);

// Save settings to SD card. Creates dirs if needed.
void settings_save(const AppSettings* s);

// Format IP into "x.x.x.x" string.
void settings_format_ip(const AppSettings* s, char* buf, int size);

#endif // SETTINGS_H
