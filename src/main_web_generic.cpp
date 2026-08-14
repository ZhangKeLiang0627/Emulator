// Generic LVGL → Web shell.
//
// Deploys ANY LVGL project (arbitrary ui_init + resolution) into the browser:
//   - Displays the project at its native resolution (LCD_W x LCD_H from CMake)
//   - Mouse + touch map to an LVGL pointer indev
//   - Physical keyboard maps to LVGL's SDL keyboard driver
//
// The only contract with the app: it must provide `extern "C" void ui_init(void)`.
// No skin, no key-map tables — just the LCD. This is intentionally ~90 lines.

#include "lvgl/lvgl.h"
#include <SDL2/SDL.h>
#include <emscripten.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void ui_init(void);
// LVGL's SDL keyboard driver (compiled into the lvgl lib, but its header isn't
// pulled in by lvgl.h in this rev — declare it directly, like the Cardputer main).
extern "C" void lv_sdl_keyboard_handler(SDL_Event *event);

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

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    uint16_t *src = (uint16_t *)px;
    for(int32_t y = 0; y < h; y++)
        for(int32_t x = 0; x < w; x++) {
            uint16_t c = src[y * w + x];
            g_buf[(area->y1 + y) * LCD_W + area->x1 + x] = 0xFF000000
                | (((c >> 11 & 0x1F) << 3 | (c >> 11 & 0x1F) >> 2) << 16)
                | (((c >> 5  & 0x3F) << 2 | (c >> 5  & 0x3F) >> 4) << 8)
                | ((c & 0x1F) << 3 | (c & 0x1F) >> 2);
        }
    lv_display_flush_ready(disp);
}

// LVGL polls this each timer tick; we just hand back the latest pointer state.
static void pointer_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
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
    lv_sdl_keyboard_handler((SDL_Event *)ev);  // physical keyboard → LVGL
}

static void main_loop(void) {
    SDL_Event ev;
    while(SDL_PollEvent(&ev)) handle_event(&ev);
    lv_tick_inc(16);
    lv_timer_handler();
    SDL_UpdateTexture(g_tex, NULL, g_buf, LCD_W * 4);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

int main(int, char *[]) {
    printf("LVGL web shell (%dx%d)\n", LCD_W, LCD_H);

    SDL_Init(SDL_INIT_VIDEO);
    g_win = SDL_CreateWindow("LVGL", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             LCD_W, LCD_H, SDL_WINDOW_SHOWN);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, LCD_W, LCD_H);
    g_buf = (uint32_t *)calloc(LCD_W * LCD_H, sizeof(uint32_t));

    lv_init();
    static uint8_t draw_buf[LCD_W * LCD_H * 2];  // full-rebuffer: simple & fine for web
    lv_display_t *disp = lv_display_create(LCD_W, LCD_H);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, pointer_read_cb);
    lv_sdl_keyboard_create();  // LVGL's SDL keyboard driver (no window needed)

    ui_init();
    printf("[EMU] Running in browser.\n");

    emscripten_set_main_loop(main_loop, 0, 1);
    return 0;
}
