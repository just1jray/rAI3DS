#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>

#define TOP_SCREEN_WIDTH 400
#define TOP_SCREEN_HEIGHT 240
#define BOTTOM_SCREEN_WIDTH 320
#define BOTTOM_SCREEN_HEIGHT 240

int main(int argc, char* argv[]) {
    // Initialize services
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // Create render targets
    C3D_RenderTarget* topScreen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottomScreen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // Colors
    u32 clrClear = C2D_Color32(0x1a, 0x1a, 0x2e, 0xFF);  // Dark blue background
    u32 clrWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);

    // Text buffer
    C2D_TextBuf textBuf = C2D_TextBufNew(256);
    C2D_Text txtTitle, txtStatus;

    // Prepare text
    C2D_TextParse(&txtTitle, textBuf, "rAI3DS v0.1.0");
    C2D_TextParse(&txtStatus, textBuf, "Press START to exit");
    C2D_TextOptimize(&txtTitle);
    C2D_TextOptimize(&txtStatus);

    // Main loop
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_START)
            break;

        // Render top screen
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(topScreen, clrClear);
        C2D_SceneBegin(topScreen);

        C2D_DrawText(&txtTitle, C2D_WithColor, 150.0f, 100.0f, 0.0f, 1.0f, 1.0f, clrWhite);

        // Render bottom screen
        C2D_TargetClear(bottomScreen, clrClear);
        C2D_SceneBegin(bottomScreen);

        C2D_DrawText(&txtStatus, C2D_WithColor, 80.0f, 110.0f, 0.0f, 0.8f, 0.8f, clrWhite);

        C3D_FrameEnd(0);
    }

    // Cleanup
    C2D_TextBufDelete(textBuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
