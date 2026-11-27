// Plays the patterns codec_test.js wrote through the firmware's own
// pattern_player.c, built for the host, and checks every frame against
// what the web interface's decoder produced.
//
//   cc -DLA04_RAM_IN_BSS ... player_test.c ../../pattern_player.c ../../la04.c ../../layout.c
//   ./player_test DIR NAME...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ch643_ledpwm.h"
#include "pattern_player.h"

#define PIXELS (LED_MATRIX_ROWS * LED_MATRIX_COLS)
static uint8_t s_shown[PIXELS];

void LEDMatrix_SetFrame(const uint8_t* frame) { memcpy(s_shown, frame, PIXELS); }

static uint8_t* load(const char* path, long* len) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    rewind(f);
    uint8_t* buf = malloc(*len);
    if (fread(buf, 1, *len, f) != (size_t)*len) exit(2);
    fclose(f);
    return buf;
}

int main(int argc, char** argv) {
    int failures = 0;
    for (int a = 2; a < argc; a++) {
        char path[512];
        long len, flen;
        snprintf(path, sizeof path, "%s/%s.ledanim", argv[1], argv[a]);
        uint8_t* anim = load(path, &len);
        snprintf(path, sizeof path, "%s/%s.frames", argv[1], argv[a]);
        uint8_t* want = load(path, &flen);
        long frames = flen / PIXELS;
        unsigned frame_ms = 1000u / anim[6];

        PatternPlayer_Start(anim, (uint32_t)len);
        int bad = -1;
        for (long f = 0; f < 2 * frames && bad < 0; f++) {  // twice: the loop back to frame 0 too
            PatternPlayer_Tick(frame_ms);
            if (memcmp(s_shown, want + (f % frames) * PIXELS, PIXELS)) bad = (int)f;
        }
        printf("%s %s: %ld frames", bad < 0 ? "ok  " : "FAIL", argv[a], frames);
        if (bad >= 0) printf(", frame %d differs", bad);
        printf("\n");
        failures += bad >= 0;
        free(anim);
        free(want);
    }
    return failures != 0;
}
