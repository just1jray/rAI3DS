#include "settings.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define SETTINGS_DIR  "sdmc:/3ds/raids"
#define SETTINGS_PATH "sdmc:/3ds/raids/config.txt"

static void parse_default_ip(AppSettings* s) {
    // Parse compile-time default from config.h
    int a = 192, b = 168, c = 1, d = 100;
    sscanf(SERVER_HOST, "%d.%d.%d.%d", &a, &b, &c, &d);
    s->octets[0] = a;
    s->octets[1] = b;
    s->octets[2] = c;
    s->octets[3] = d;
    s->port = SERVER_PORT;
}

bool settings_load(AppSettings* s) {
    parse_default_ip(s);

    FILE* f = fopen(SETTINGS_PATH, "r");
    if (!f) return false;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int a, b, c, d;
        if (sscanf(line, "ip=%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
            s->octets[0] = a;
            s->octets[1] = b;
            s->octets[2] = c;
            s->octets[3] = d;
        }
        int p;
        if (sscanf(line, "port=%d", &p) == 1) {
            s->port = p;
        }
    }

    fclose(f);
    return true;
}

void settings_save(const AppSettings* s) {
    mkdir("sdmc:/3ds", 0755);
    mkdir(SETTINGS_DIR, 0755);

    FILE* f = fopen(SETTINGS_PATH, "w");
    if (!f) return;

    fprintf(f, "ip=%d.%d.%d.%d\n", s->octets[0], s->octets[1], s->octets[2], s->octets[3]);
    fprintf(f, "port=%d\n", s->port);
    fclose(f);
}

void settings_format_ip(const AppSettings* s, char* buf, int size) {
    snprintf(buf, size, "%d.%d.%d.%d", s->octets[0], s->octets[1], s->octets[2], s->octets[3]);
}
