// LVGL v8 → Web shell (for projects that bundle their own LVGL v8, e.g. app/guiproc).
//
// Same shape as main_web_generic.cpp but using the v8 HAL (lv_disp_drv_t /
// lv_disp_draw_buf_t / lv_indev_drv_t) and assuming LV_COLOR_DEPTH == 32:
//   - v8 has no lv_sdl_keyboard driver → mouse + touch only
//   - lv_color_t is ARGB8888, byte-identical to the SDL texture → verbatim copy
//
// Logical-vs-physical resolution: the UI is designed at LCD_W x LCD_H (logical,
// e.g. guiproc 1280x720), but the panel is PHY_W x PHY_H (e.g. 800x480). Like
// the firmware's fbdev_flush_scaled, the rendered logical frame is nearest-
// neighbor downscaled to the physical canvas in flush_cb, and pointer input is
// mapped back physical → logical.
//
// The app contract is `extern "C" void web_ui_init(void)`.

#include "lvgl.h"
#include <SDL2/SDL.h>
#include <emscripten.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

extern "C" void web_ui_init(void);

#ifndef LCD_W
#define LCD_W 1280
#endif
#ifndef LCD_H
#define LCD_H 720
#endif
// Physical canvas size. Defaults to the logical size (no scaling).
#ifndef PHY_W
#define PHY_W LCD_W
#endif
#ifndef PHY_H
#define PHY_H LCD_H
#endif

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static uint32_t     *g_buf;

// ── pointer state, fed to LVGL by the read callback ────────────────────────
static int   g_px = 0, g_py = 0;   // logical (LCD) coords
static bool  g_pressed = false;

// Nearest-neighbor scale of the LVGL area into the physical canvas — mirrors
// the firmware's fbdev_flush_scaled mapping (non-uniform sx/sy).
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    int32_t src_w = drv->hor_res, src_h = drv->ver_res;
    int32_t dst_w = PHY_W, dst_h = PHY_H;

    int32_t act_x1 = area->x1 < 0 ? 0 : area->x1;
    int32_t act_y1 = area->y1 < 0 ? 0 : area->y1;
    int32_t act_x2 = area->x2 > src_w - 1 ? src_w - 1 : area->x2;
    int32_t act_y2 = area->y2 > src_h - 1 ? src_h - 1 : area->y2;
    if(act_x1 > act_x2 || act_y1 > act_y2) { lv_disp_flush_ready(drv); return; }

    int32_t src_stride = area->x2 - area->x1 + 1;
    const uint32_t *src_base = (const uint32_t *)px
        + (act_y1 - area->y1) * src_stride + (act_x1 - area->x1);

    int32_t dst_x1 = (int32_t)(((int64_t)act_x1 * dst_w) / src_w);
    int32_t dst_x2 = (int32_t)((((int64_t)(act_x2 + 1) * dst_w) - 1) / src_w);
    int32_t dst_y1 = (int32_t)(((int64_t)act_y1 * dst_h) / src_h);
    int32_t dst_y2 = (int32_t)((((int64_t)(act_y2 + 1) * dst_h) - 1) / src_h);
    if(dst_x1 < 0) dst_x1 = 0; if(dst_y1 < 0) dst_y1 = 0;
    if(dst_x2 > dst_w - 1) dst_x2 = dst_w - 1; if(dst_y2 > dst_h - 1) dst_y2 = dst_h - 1;
    if(dst_x1 > dst_x2 || dst_y1 > dst_y2) { lv_disp_flush_ready(drv); return; }

    for(int32_t dy = dst_y1; dy <= dst_y2; ++dy) {
        int32_t sy = (int32_t)(((int64_t)dy * src_h) / dst_h);
        if(sy < act_y1 || sy > act_y2) continue;
        int32_t src_row = sy - act_y1;
        for(int32_t dx = dst_x1; dx <= dst_x2; ++dx) {
            int32_t sx = (int32_t)(((int64_t)dx * src_w) / dst_w);
            if(sx < act_x1 || sx > act_x2) continue;
            g_buf[(int64_t)dy * dst_w + dx] =
                src_base[(int64_t)src_row * src_stride + (sx - act_x1)];
        }
    }
    lv_disp_flush_ready(drv);
}

// LVGL polls this each timer tick; we just hand back the latest pointer state.
static void pointer_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    data->point.x = g_px;
    data->point.y = g_py;
    data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// Physical canvas pixel → logical UI pixel.
static int to_logical_x(int px) { return (int)(((int64_t)px * LCD_W) / PHY_W); }
static int to_logical_y(int py) { return (int)(((int64_t)py * LCD_H) / PHY_H); }

static void handle_event(const SDL_Event *ev) {
    switch(ev->type) {
        case SDL_MOUSEMOTION:
            g_px = to_logical_x(ev->motion.x); g_py = to_logical_y(ev->motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if(ev->button.button == SDL_BUTTON_LEFT) {
                g_px = to_logical_x(ev->button.x); g_py = to_logical_y(ev->button.y);
                g_pressed = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if(ev->button.button == SDL_BUTTON_LEFT) g_pressed = false;
            break;
        // Touch: SDL normalizes to 0..1 across the canvas → map straight to logical.
        case SDL_FINGERDOWN:
            g_px = (int)(ev->tfinger.x * LCD_W);
            g_py = (int)(ev->tfinger.y * LCD_H);
            g_pressed = true;
            break;
        case SDL_FINGERUP:
            g_pressed = false;
            break;
        case SDL_FINGERMOTION:
            g_px = (int)(ev->tfinger.x * LCD_W);
            g_py = (int)(ev->tfinger.y * LCD_H);
            break;
    }
}

static void main_loop(void) {
    SDL_Event ev;
    while(SDL_PollEvent(&ev)) handle_event(&ev);
    lv_timer_handler();  // ticks come from custom_tick_get() (LV_TICK_CUSTOM)
    SDL_UpdateTexture(g_tex, NULL, g_buf, PHY_W * 4);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

int main(int, char *[]) {
    printf("LVGL v8 web shell (logical %dx%d → canvas %dx%d)\n", LCD_W, LCD_H, PHY_W, PHY_H);

    SDL_Init(SDL_INIT_VIDEO);
    g_win = SDL_CreateWindow("LVGL", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             PHY_W, PHY_H, SDL_WINDOW_SHOWN);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, PHY_W, PHY_H);
    g_buf = (uint32_t *)calloc((size_t)PHY_W * PHY_H, sizeof(uint32_t));

    lv_init();  // auto: lv_extra_init → lv_fs_posix_init, lv_png_init, lv_freetype_init

    static lv_color_t draw_buf[LCD_W * LCD_H];  // full-rebuffer: simple & fine for web
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, draw_buf, NULL, LCD_W * LCD_H);  // size = pixel count

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = LCD_W;
    disp_drv.ver_res  = LCD_H;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = flush_cb;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = pointer_read_cb;
    lv_indev_drv_register(&indev_drv);
    // v8 has no SDL keyboard driver — mouse + touch only.

    web_ui_init();
    printf("[EMU] Running in browser.\n");

    emscripten_set_main_loop(main_loop, 0, 1);
    return 0;
}
