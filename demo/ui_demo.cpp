// Minimal touch-reactive LVGL app for the generic web shell.
// Proves the input path end-to-end: tap the button → LV_EVENT_CLICKED fires
// with real pointer coordinates.
#include "lvgl/lvgl.h"

static lv_obj_t *tap_label;
static lv_obj_t *pos_label;
static int taps = 0;

static void on_tap(lv_event_t *e) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    taps++;
    lv_label_set_text_fmt(tap_label, "Taps: %d", taps);
    lv_label_set_text_fmt(pos_label, "pointer %d, %d", p.x, p.y);
}

extern "C" void ui_init(void) {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101018), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "LVGL on the web");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xcccccc), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 240, 72);
    lv_obj_center(btn);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a73e8), 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_add_event_cb(btn, on_tap, LV_EVENT_CLICKED, NULL);

    tap_label = lv_label_create(btn);
    lv_label_set_text(tap_label, "Tap me");
    lv_obj_set_style_text_font(tap_label, &lv_font_montserrat_24, 0);
    lv_obj_center(tap_label);

    pos_label = lv_label_create(scr);
    lv_label_set_text(pos_label, "");
    lv_obj_set_style_text_font(pos_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pos_label, lv_color_hex(0x888888), 0);
    lv_obj_align(pos_label, LV_ALIGN_BOTTOM_MID, 0, -20);
}
