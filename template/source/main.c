/*
 * xenon-hello-test - Etapa 4: playable 2D platformer micro-prototype for
 * Xbox 360 (RGH + XeLL). Built remotely in free60/libxenon:latest.
 *
 * Drawing path: libXenon has NO OpenGL and NO high-level 2D library, so pixels
 * are written one by one into the framebuffer console (console_pset) and the
 * CPU data cache is flushed with memdcbst, exactly like console.c itself does.
 *
 * The scene (sky, ground, one platform) is drawn ONCE at startup. Each frame
 * only the player's old area is repainted with the background and the player
 * is drawn at its new position; only the affected 32x32 tiles are flushed from
 * the cache. This removes the full-screen clear+redraw of Etapa 2.
 *
 * Frame time is measured on the console with the PowerPC timebase (mftb) and
 * the average is printed periodically, so no fps claim is made that was not
 * measured on real hardware.
 *
 * Controls: D-pad / left stick move; A or D-pad up jumps; Guide or Y quits.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include <console/console.h>
#include <input/input.h>
#include <usb/usbmain.h>
#include <xenon_soc/xenon_power.h>
#include <xenos/xenos.h>
#include <time/time.h>
#include <ppc/cache.h>
#include <ppc/timebase.h>

/* Mirror of the display-info block console.c reads (libxenon/drivers/console). */
struct fb_ati_info {
    uint32_t unknown1[4];
    uint32_t base;
    uint32_t unknown2[8];
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

#define FB_INFO_ADDR 0xec806100UL

/* Player */
#define PLAYER_W  40
#define PLAYER_H  64
#define HEAD_H    14

/* Level geometry */
#define GROUND_H  54
#define PLAT_W    280
#define PLAT_H    22
#define PLAT_GAP  120   /* platform top, above the ground top */

/* Physics (values per ~60 fps frame; tuned for this prototype) */
#define GRAVITY   0.60f
#define JUMP_VY  -14.0f
#define MAX_FALL  14.0f
#define MOVE_SPD  3.4f
#define AI_DEAD   0.20f

#define LOOP_MS   16
#define FPS_WINDOW 150

#define C_R 0x08, 0x10, 0x28 /* sky          */
#define GND 0x22, 0x14, 0x08 /* ground       */
#define PLT 0x30, 0x30, 0x38 /* platform     */
#define BOD 0x20, 0xF0, 0x40 /* player body  */
#define HED 0xFF, 0xE0, 0x00 /* player head  */

static unsigned char *g_fb;
static int g_fb_w;          /* both rounded up to 32-pixel tiles */
static int g_fb_h;
static int g_fb_bytes;
static int g_off_x, g_off_y; /* overscan offset, replicated from console.c */
static int g_screen_w, g_screen_h;

static int ground_top;
static int plat_x, plat_y;

static float px, py, pvx, pvy;

static void fill_rect(int x0, int y0, int x1, int y1,
                      unsigned char r, unsigned char g, unsigned char b)
{
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g_screen_w) x1 = g_screen_w;
    if (y1 > g_screen_h) y1 = g_screen_h;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            console_pset(x, y, r, g, b);
}

static void draw_scene(void)
{
    fill_rect(0, 0, g_screen_w, g_screen_h, C_R);
    fill_rect(0, ground_top, g_screen_w, g_screen_h, GND);
    fill_rect(plat_x, plat_y, plat_x + PLAT_W, plat_y + PLAT_H, PLT);
}

static void paint_bg(int x0, int y0, int x1, int y1)
{
    fill_rect(x0, y0, x1, y1, C_R);
    if (y1 > ground_top)
        fill_rect(x0, y0 > ground_top ? y0 : ground_top, x1, y1, GND);
    if (x1 > plat_x && x0 < plat_x + PLAT_W && y1 > plat_y && y0 < plat_y + PLAT_H)
        fill_rect(x0 > plat_x ? x0 : plat_x,
                  y0 > plat_y ? y0 : plat_y,
                  x1 < plat_x + PLAT_W ? x1 : plat_x + PLAT_W,
                  y1 < plat_y + PLAT_H ? y1 : plat_y + PLAT_H, PLT);
}

static void draw_player(int x, int y)
{
    fill_rect(x, y + HEAD_H, x + PLAYER_W, y + PLAYER_H, BOD);
    fill_rect(x + (PLAYER_W - 22) / 2, y,
              x + (PLAYER_W + 22) / 2, y + HEAD_H, HED);
}

/* Whole-framebuffer cache flush (same as console_clrscr/console_putch). */
static void fb_flush(void)
{
    if (g_fb && g_fb_bytes)
        memdcbst(g_fb, g_fb_bytes);
}

/*
 * Flush only the cache lines of the 32x32 tiles touched by the given
 * (exclusive) screen rect. Tile layout (see console_pset32 in console.c):
 * tile bytes live contiguously, tile column stride 4096, tile row stride
 * g_fb_w*128. Flushing the byte span of the bounding box of touched tiles
 * covers everything we wrote.
 */
static void flush_tiles(int x0, int y0, int x1, int y1)
{
    int px0 = x0 + g_off_x;
    int py0 = y0 + g_off_y;
    int px1 = x1 + g_off_x;
    int py1 = y1 + g_off_y;
    unsigned int start, end;

    if (px0 < 0) px0 = 0;
    if (py0 < 0) py0 = 0;
    if (px1 > g_fb_w) px1 = g_fb_w;
    if (py1 > g_fb_h) py1 = g_fb_h;
    if (px1 <= px0 || py1 <= py0)
        return;

    start = (unsigned int)(((py0 >> 5) * (g_fb_w * 128)) + ((px0 >> 5) * 4096));
    end   = (unsigned int)((((py1 - 1) >> 5) * (g_fb_w * 128)) +
                            (((px1 - 1) >> 5) * 4096)) + 4096U;
    if (end > (unsigned int)g_fb_bytes)
        end = (unsigned int)g_fb_bytes;
    if (end > start)
        memdcbst(g_fb + start, end - start);
}

int main(void)
{
    struct fb_ati_info *ai;
    unsigned int cw = 0, ch = 0;
    bool prev_jump = false;
    struct controller_data_s pad;
    uint64_t t0, fps_sum_ms = 0;
    int fps_frames = 0;

    xenos_init(VIDEO_MODE_AUTO);
    console_init();
    xenon_make_it_faster(XENON_SPEED_FULL);

    usb_init();
    usb_do_poll();

    ai = (struct fb_ati_info *)(uintptr_t)FB_INFO_ADDR;
    g_fb = (unsigned char *)(uintptr_t)(ai->base | 0x80000000U);
    g_fb_w = ((ai->width + 31) >> 5) << 5;
    g_fb_h = ((ai->height + 31) >> 5) << 5;
    g_fb_bytes = g_fb_w * g_fb_h * 4;

    g_off_x = g_off_y = 0;
    if (xenos_is_overscan())
    {
        g_off_x = ai->width / 28;
        g_off_y = ai->height / 28;
    }

    console_get_dimensions(&cw, &ch);
    g_screen_w = (int)cw * 8;
    g_screen_h = (int)ch * 16;
    if (g_screen_w < PLAYER_W + 2)
        g_screen_w = PLAYER_W + 2;
    if (g_screen_h < GROUND_H + PLAYER_H)
        g_screen_h = GROUND_H + PLAYER_H;

    ground_top = g_screen_h - GROUND_H;
    plat_x = (g_screen_w - PLAT_W) / 2;
    if (plat_x < 0) plat_x = 0;
    plat_y = ground_top - PLAT_GAP;
    if (plat_y < 40) plat_y = 40;

    px = (float)(g_screen_w / 4);
    py = (float)(ground_top - PLAYER_H);

    draw_scene();
    draw_player((int)(px + 0.5f), (int)(py + 0.5f));
    fb_flush();

    console_set_cursor(0, 0);
    printf("Etapa 4: 2D platform prototype (xenon-hello-test)\n");
    printf("Dpad/analog: move   A/Cima: jump   Guide/Y: sair\n");
    printf("screen=%dx%d  ground=%d  platform_y=%d\n",
           g_screen_w, g_screen_h, ground_top, plat_y);

    while (true)
    {
        float ax;
        bool want_jump;
        int old_ix, old_iy, new_ix, new_iy;

        t0 = mftb();
        usb_do_poll();

        old_ix = (int)(px + 0.5f);
        old_iy = (int)(py + 0.5f);

        if (get_controller_data(&pad, 0))
        {
            if (pad.logo || pad.y)
                exit(0);

            pvx = 0.0f;
            if (pad.left)       pvx = -MOVE_SPD;
            else if (pad.right) pvx =  MOVE_SPD;
            else
            {
                ax = (float)pad.s1_x / 32768.0f;
                if (ax >  AI_DEAD) pvx =  ax * MOVE_SPD;
                else if (ax < -AI_DEAD) pvx =  ax * MOVE_SPD;
            }

            px += pvx;
            if (px < 0.0f) { px = 0.0f; pvx = 0.0f; }
            else if (px > (float)(g_screen_w - PLAYER_W))
            {
                px = (float)(g_screen_w - PLAYER_W);
                pvx = 0.0f;
            }

            {
                float prev_bottom = py + PLAYER_H;
                bool on_ground = false;

                pvy += GRAVITY;
                if (pvy > MAX_FALL) pvy = MAX_FALL;
                py += pvy;

                if (pvy > 0.0f)
                {
                    float bottom = py + PLAYER_H;
                    if (px + PLAYER_W > plat_x && px < plat_x + PLAT_W &&
                        prev_bottom <= plat_y + 0.5f && bottom >= plat_y)
                    {
                        py = (float)plat_y - PLAYER_H;
                        pvy = 0.0f;
                        on_ground = true;
                    }
                    if (bottom >= ground_top)
                    {
                        py = (float)(ground_top - PLAYER_H);
                        pvy = 0.0f;
                        on_ground = true;
                    }
                }

                want_jump = pad.a || pad.up;
                if (want_jump && !prev_jump && on_ground)
                    pvy = JUMP_VY;
            }
        }
        else
        {
            want_jump = false;
        }

        prev_jump = want_jump;

        new_ix = (int)(px + 0.5f);
        new_iy = (int)(py + 0.5f);
        if (new_ix != old_ix || new_iy != old_iy)
        {
            paint_bg(old_ix, old_iy, old_ix + PLAYER_W, old_iy + PLAYER_H);
            draw_player(new_ix, new_iy);
            flush_tiles(old_ix, old_iy, old_ix + PLAYER_W, old_iy + PLAYER_H);
            flush_tiles(new_ix, new_iy, new_ix + PLAYER_W, new_iy + PLAYER_H);
        }

        mdelay(LOOP_MS);

        fps_frames++;
        fps_sum_ms += tb_diff_msec(mftb(), t0);
        if (fps_frames >= FPS_WINDOW)
        {
            /*
             * Integer-only stats (some libXenon printf variants skip %f):
             * fps_x10 = fps*10, frame_ms printed as dd.cc.
             */
            unsigned long total_ms = (unsigned long)fps_sum_ms;
            int fps_x10 = (int)((10000UL * (unsigned long)fps_frames) / total_ms);
            int fms_hi  = (int)(total_ms / (unsigned long)fps_frames);
            int fms_lo  = (int)(((total_ms % (unsigned long)fps_frames) * 100UL) /
                                (unsigned long)fps_frames);
            console_set_cursor(0, 3);
            printf("fps: %2d.%1d   frame_avg_ms: %d.%02d            ",
                   fps_x10 / 10, fps_x10 % 10, fms_hi, fms_lo);
            fps_frames = 0;
            fps_sum_ms = 0;
        }
    }

    return 0;
}