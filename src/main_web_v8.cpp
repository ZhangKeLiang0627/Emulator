// LVGL v8 → Web shell (for projects that bundle their own LVGL v8, e.g. app/guiproc).
//
// Same shape as main_web_generic.cpp but using the v8 HAL (lv_disp_drv_t /
// lv_disp_draw_buf_t / lv_indev_drv_t) and assuming LV_COLOR_DEPTH == 32:
//   - v8 has no lv_sdl_keyboard driver → mouse + touch only
//   - lv_color_t is ARGB8888, byte-identical to the SDL texture → verbatim copy
// The app contract is `extern "C" void web_ui_init(void)` (a void() ui_init would
// collide with the app's own ui_init(InitConfig_t)).

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

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static uint32_t     *g_buf;

// ── pointer state, fed to LVGL by the read callback ────────────────────────
static int   g_px = 0, g_py = 0;
static bool  g_pressed = false;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    // LV_COLOR_DEPTH==32 → lv_color_t is ARGB8888 == SDL texture format, copy verbatim.
    const uint32_t *src = (const uint32_t *)px;
    for(int32_t y = 0; y < h; y++)
        memcpy(&g_buf[(area->y1 + y) * LCD_W + area->x1], &src[y * w],
               (size_t)w * sizeof(uint32_t));
    lv_disp_flush_ready(drv);
}

// LVGL polls this each timer tick; we just hand back the latest pointer state.
static void pointer_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    data->point.x = g_px;
    data->point.y = g_py;
    data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void handle_event(const SDL_Event *ev) {
    switch(ev->type) {
        case SDL_MOUSEMOTION:
            g_px = ev->motion.x; g_py = ev->motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if(ev->button.button == SDL_BUTTON_LEFT) {
                g_px = ev->button.x; g_py = ev->button.y;
                g_pressed = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if(ev->button.button == SDL_BUTTON_LEFT) g_pressed = false;
            break;
        // Touch: SDL normalizes to 0..1 across the canvas.
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
    SDL_UpdateTexture(g_tex, NULL, g_buf, LCD_W * 4);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

int main(int, char *[]) {
    printf("LVGL v8 web shell (%dx%d)\n", LCD_W, LCD_H);

    SDL_Init(SDL_INIT_VIDEO);
    g_win = SDL_CreateWindow("LVGL", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             LCD_W, LCD_H, SDL_WINDOW_SHOWN);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, LCD_W, LCD_H);
    g_buf = (uint32_t *)calloc((size_t)LCD_W * LCD_H, sizeof(uint32_t));

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
