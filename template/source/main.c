/*
 * xenon-hello-test - Etapa 2: 2D flat-colour rectangle POC for Xbox 360 (RGH + XeLL).
 *
 * Independent demo. Built remotely in free60/libxenon:latest.
 *
 * IMPORTANT (API reality check):
 *   libXenon has NO OpenGL and NO high-level 2D drawing library. The only
 *   usable drawing path without hand-written GPU shaders is the framebuffer
 *   console: console.h exports
 *       void console_pset(int x, int y, unsigned char r, unsigned char g, unsigned char b);
 *   which writes a single 32bpp pixel into the SAME Xenos framebuffer the text
 *   console uses (tiled BGRA layout + overscan offset handled internally).
 *   After writing pixels from the CPU we flush the data cache (ppc/cache.h,
 *   memdcbst) so the display engine sees them - the exact technique console.c
 *   itself uses. This is the minimal, real alternative to a graphics API.
 *
 * Controls:  D-pad moves the rectangle;  Guide or Y exits.
 * The Etapa 1 "Hello world" program is preserved in git history
 * (commit "Add minimal libXenon hello/controller test with remote CI build").
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <console/console.h>
#include <input/input.h>
#include <usb/usbmain.h>
#include <xenon_soc/xenon_power.h>
#include <xenos/xenos.h>
#include <time/time.h>
#include <ppc/cache.h>

/*
 * Mirror of the display-info block that console.c reads to find the
 * framebuffer. Only `base`, `width` and `height` are used here.
 */
struct fb_ati_info {
    uint32_t unknown1[4];
    uint32_t base;
    uint32_t unknown2[8];
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

#define FB_INFO_ADDR 0xec806100UL

#define RECT_W 180
#define RECT_H 120
#define RECT_STEP 16

static unsigned char *g_fb = 0;   /* CPU pointer to the framebuffer        */
static int g_fb_bytes = 0;        /* size in bytes, for the cache flush    */
static int g_screen_w = 0;        /* usable width in pixels (no overscan)  */
static int g_screen_h = 0;        /* usable height in pixels               */

static int g_rect_x = 0;
static int g_rect_y = 0;

/* Flush the whole framebuffer from the CPU data cache (see console_clrscr). */
static void fb_flush(void)
{
    if (g_fb && g_fb_bytes)
        memdcbst(g_fb, g_fb_bytes);
}

/* Filled rectangle, one pixel at a time through the console framebuffer path. */
static void draw_rect(int x0, int y0, int w, int h,
                      unsigned char r, unsigned char g, unsigned char b)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            console_pset(x0 + x, y0 + y, r, g, b);
}

static void redraw(void)
{
    /* dark background */
    console_clrscr();

    console_set_cursor(0, 0);
    printf("xenon-hello-test - Etapa 2 (2D POC)\n");
    printf("D-pad: mover retangulo   Guide/Y: sair\n");
    printf("pos: %4d,%4d   tela: %dx%d\n", g_rect_x, g_rect_y, g_screen_w, g_screen_h);

    /* bright rectangle on the dark background */
    draw_rect(g_rect_x, g_rect_y, RECT_W, RECT_H, 0xFF, 0x30, 0x00);

    fb_flush();
}

int main(void)
{
    /* Video + text console (framebuffer). */
    xenos_init(VIDEO_MODE_AUTO);
    console_init();

    /* Full 3.2 GHz. */
    xenon_make_it_faster(XENON_SPEED_FULL);

    /* USB / gamepad. */
    usb_init();
    usb_do_poll();

    /* Discover the framebuffer (same source console.c uses). */
    {
        struct fb_ati_info *ai = (struct fb_ati_info *)(uintptr_t)FB_INFO_ADDR;
        unsigned int cw = 0, ch = 0;

        g_fb = (unsigned char *)(uintptr_t)(ai->base | 0x80000000U);
        g_fb_bytes = (int)((((ai->width + 31) >> 5) << 5) *
                           (((ai->height + 31) >> 5) << 5) * 4);

        /* usable pixel area = text-console dimensions (already minus overscan) */
        console_get_dimensions(&cw, &ch);
        g_screen_w = (int)cw * 8;
        g_screen_h = (int)ch * 16;

        if (g_screen_w < RECT_W + 2) g_screen_w = RECT_W + 2;
        if (g_screen_h < RECT_H + 2) g_screen_h = RECT_H + 2;

        g_rect_x = (g_screen_w - RECT_W) / 2;
        g_rect_y = (g_screen_h - RECT_H) / 2;
    }

    redraw();

    struct controller_data_s pad;

    while (true)
    {
        usb_do_poll();

        if (get_controller_data(&pad, 0))
        {
            /* Exit: Guide (logo) or Y - unchanged from Etapa 1. */
            if (pad.logo || pad.y)
                exit(0);

            int dx = 0, dy = 0;
            if (pad.left)  dx -= RECT_STEP;
            if (pad.right) dx += RECT_STEP;
            if (pad.up)    dy -= RECT_STEP;
            if (pad.down)  dy += RECT_STEP;

            if (dx || dy)
            {
                int nx = g_rect_x + dx;
                int ny = g_rect_y + dy;

                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx > g_screen_w - RECT_W) nx = g_screen_w - RECT_W;
                if (ny > g_screen_h - RECT_H) ny = g_screen_h - RECT_H;

                if (nx != g_rect_x || ny != g_rect_y)
                {
                    g_rect_x = nx;
                    g_rect_y = ny;
                    redraw();
                }
            }
        }

        /* ~50 Hz loop; also throttles movement to a controllable speed. */
        mdelay(20);
    }

    return 0;
}
