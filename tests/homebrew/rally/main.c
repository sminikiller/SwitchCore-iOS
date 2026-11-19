// SPDX-License-Identifier: GPL-3.0-only
#include <switch.h>

enum { Width = 320, Height = 180 };
static const u32 sea = 0xff352316, active = 0xff694c19;
static const u32 line = 0xff786047, mint = 0xffbbef68, white = 0xffecf5ed;

static void rect(u32 *pixels, u32 pitch, int x, int y, int w, int h, u32 color) {
    for (int row = y; row < y + h; ++row)
        for (int col = x; col < x + w; ++col)
            pixels[row * pitch + col] = color;
}

int main(void) {
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    Framebuffer fb;
    if (R_FAILED(framebufferCreate(&fb, nwindowGetDefault(), Width, Height, PIXEL_FORMAT_RGBA_8888, 2)))
        return 1;
    if (R_FAILED(framebufferMakeLinear(&fb))) { framebufferClose(&fb); return 2; }
    int paddle = 68, x = 160, y = 90, dx = -2, dy = 1, points = 0;
    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 keys = padGetButtons(&pad);
        if (keys & HidNpadButton_Plus) break;
        if ((keys & (HidNpadButton_Up | HidNpadButton_StickLUp)) && paddle > 3) paddle -= 3;
        if ((keys & (HidNpadButton_Down | HidNpadButton_StickLDown)) && paddle < Height - 44) paddle += 3;
        x += dx; y += dy;
        if (y <= 3 || y >= Height - 7) dy = -dy;
        if (x >= Width - 9) dx = -2;
        if (x == 16 && dx < 0 && y + 4 >= paddle && y <= paddle + 40) { dx = 2; ++points; }
        if (x < 2) { x = 160; y = 90; dx = -2; points = 0; }
        u32 stride;
        u32 *pixels = framebufferBegin(&fb, &stride);
        const u32 pitch = stride / sizeof(*pixels);
        rect(pixels, pitch, 0, 0, Width, Height, keys & HidNpadButton_A ? active : sea);
        for (int row = 5; row < Height - 5; row += 10) rect(pixels, pitch, 159, row, 1, 4, line);
        rect(pixels, pitch, 10, paddle, 6, 40, mint);
        rect(pixels, pitch, Width - 4, 2, 2, Height - 4, mint);
        rect(pixels, pitch, x, y, 4, 4, white);
        rect(pixels, pitch, 12, 4, points % 80, 2, white);
        framebufferEnd(&fb);
    }
    framebufferClose(&fb);
    return 0;
}

