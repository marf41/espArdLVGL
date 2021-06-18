#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <lvgl.h>

#define INPUT_JOY_UP 32
#define INPUT_JOY_DOWN 33
#define INPUT_JOY_LEFT 34
#define INPUT_JOY_RIGHT 36
#define INPUT_JOY_OK 39

#define ADC_BATTERY 35
#define ADC_SENSOR 13

#if LV_USE_LOG != 0
void my_print(lv_log_level_t level, const char *file, uint32_t line,
              const char *fn_name, const char *dsc) {
    Serial.printf("%s(%s)@%d->%s\r\n", file, fn_name, line, dsc);
    Serial.flush();
}
#endif

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, 21, 22);

static lv_disp_draw_buf_t disp_buf;
static lv_color_t buf[LV_HOR_RES_MAX * 10];
lv_disp_drv_t disp_drv;
lv_indev_drv_t indev_drv;
static lv_indev_t *indev;

static const lv_font_t *font_normal;
static lv_obj_t *uplabel;
static lv_obj_t *batr;
static lv_obj_t *batico;
static lv_obj_t *tv;
static lv_obj_t *slider;
static lv_obj_t *curr;
static lv_obj_t *maxa;
static lv_obj_t *currval;
static lv_obj_t *maxval;
static lv_style_t nonmono;

void slider_event_handler(lv_event_t *e) {
    LV_UNUSED(e);
    Serial.printf("Slider: %d\n", lv_slider_get_value(slider));
}

void update_uptime(lv_timer_t *task) {
    char ut[64] = {0};
    time_t now;
    time(&now);
    time_t m = now / 60;
    if (m / 60 > 99) {
        sprintf(ut, ">99h :%02ld", now % 60);
    } else {
        sprintf(ut, "%02ld:%02ld:%02ld", m / 60, m % 60, now % 60);
    }
    lv_label_set_text(uplabel, ut);
    uint16_t v = analogRead(ADC_BATTERY);
    float bv = ((float)v / 4095.0) * 2.0 * 3.3 * (1100.0 / 1000.0);
    sprintf(ut, "%.2fV", bv);
    lv_label_set_text(batr, ut);
    if (bv >= 4.3) {
        lv_label_set_text(batico, LV_SYMBOL_BATTERY_FULL);
    } else if (bv >= 4.0) {
        lv_label_set_text(batico, LV_SYMBOL_BATTERY_3);
    } else if (bv >= 3.7) {
        lv_label_set_text(batico, LV_SYMBOL_BATTERY_2);
    } else if (bv >= 3.3) {
        lv_label_set_text(batico, LV_SYMBOL_BATTERY_1);
    } else {
        lv_label_set_text(batico, LV_SYMBOL_BATTERY_EMPTY);
    }
}

uint8_t prevkey;

void joy_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_PRESSED;
    if (digitalRead(INPUT_JOY_OK) == LOW) {
        data->key = LV_KEY_ENTER;
    } else if (digitalRead(INPUT_JOY_RIGHT) == LOW) {
        data->key = LV_KEY_RIGHT;
    } else if (digitalRead(INPUT_JOY_LEFT) == LOW) {
        data->key = LV_KEY_LEFT;
    } else if (digitalRead(INPUT_JOY_UP) == LOW) {
        data->key = LV_KEY_PREV;
    } else if (digitalRead(INPUT_JOY_DOWN) == LOW) {
        data->key = LV_KEY_NEXT;
    } else {
        data->key = prevkey;
        data->state = LV_INDEV_STATE_RELEASED;
    }
    prevkey = data->key;
}

void oled_flush(lv_disp_drv_t *drv, const lv_area_t *area,
                lv_color_t *color_p) {
    LV_LOG_INFO("OLED flush start.");
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    /*
    uint32_t wh = w * h;
    oled.drawXBM(area->x1, area->y1, w, h, color_p);
    */
    oled.setDrawColor(0);
    oled.drawBox(area->x1, area->y1, w, h);
    oled.setDrawColor(1);
    for (uint16_t y = area->y1; y <= area->y2; y++) {
        for (uint16_t x = area->x1; x <= area->x2; x++) {
            if (color_p->full > 0) {
                oled.drawPixel(x, y);
            }
            /* oled.setDrawColor(1 - color_p->full); */
            /* oled.drawPixel(x, y); */
            color_p++;
        }
    }
    oled.sendBuffer();
    lv_disp_flush_ready(drv);
    LV_LOG_INFO("OLED flush done.");
}

void oled_set_px(lv_disp_drv_t *drv, uint8_t *buf, lv_coord_t w, lv_coord_t x,
                 lv_coord_t y, lv_color_t color, lv_opa_t opa) {
    // buf += w * y + x >> 3;
    buf += x + ((y >> 3) * w);
    if (lv_color_brightness(color) > 128) {
        (*buf) |= (1 << (x & 0x7));
    } else {
        (*buf) &= ~(1 << (x & 0x7));
    }
}

void oled_rounder(lv_disp_drv_t *drv, lv_area_t *area) {
    area->y1 = (area->y1 & (~0x7));
    area->y2 = (area->y2 & (~0x7)) + 7;
}

void setup(void) {
    pinMode(INPUT_JOY_UP, INPUT);
    pinMode(INPUT_JOY_DOWN, INPUT);
    pinMode(INPUT_JOY_LEFT, INPUT);
    pinMode(INPUT_JOY_RIGHT, INPUT);
    pinMode(INPUT_JOY_OK, INPUT);
    Serial.begin(115200);
    LV_LOG_INFO("Setup start.");
    oled.begin();
    lv_init();
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * 10);
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.hor_res = 128;
    disp_drv.ver_res = 64;
    disp_drv.flush_cb = oled_flush;
    /* disp_drv.rounder_cb = oled_rounder; */
    /* disp_drv.set_px_cb = oled_set_px; */
    lv_disp_drv_register(&disp_drv);
    lv_indev_drv_init(&indev_drv);
    /* indev_drv.type = LV_INDEV_TYPE_KEYPAD; */
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = joy_read;
    indev = lv_indev_drv_register(&indev_drv);
    font_normal = LV_FONT_DEFAULT;
    lv_theme_t *th =
        lv_theme_mono_init(lv_disp_get_default(), true, &lv_font_unscii_8);
    lv_disp_set_theme(lv_disp_get_default(), th);
    lv_style_init(&nonmono);
    lv_style_set_text_font(&nonmono, &lv_font_montserrat_12);
    static lv_style_t linestyle;
    lv_style_init(&linestyle);
    lv_style_set_line_width(&linestyle, 1);
    lv_style_set_line_color(&linestyle, lv_color_white());
    /*
    lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE),
                          lv_palette_main(LV_PALETTE_RED),
                          LV_THEME_DEFAULT_DARK, font_normal);
    lv_obj_set_style_text_font(lv_scr_act(), font_normal, 0);

    tv = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 24);
    lv_obj_t *t1 = lv_tabview_add_tab(tv, "Test");
    lv_obj_t *panel = lv_obj_create(t1);
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    */

    batr = lv_label_create(lv_scr_act());
    lv_label_set_text(batr, "-");
    lv_obj_align(batr, LV_ALIGN_TOP_RIGHT, -15, 0);
    batico = lv_label_create(lv_scr_act());
    lv_label_set_text(batico, LV_SYMBOL_BATTERY_FULL);
    lv_obj_add_style(batico, &nonmono, 0);
    lv_obj_align(batico, LV_ALIGN_TOP_RIGHT, 0, -3);

    uplabel = lv_label_create(lv_scr_act());
    lv_label_set_text(uplabel, "Hello!");
    lv_obj_align(uplabel, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *sline;
    sline = lv_line_create(lv_scr_act());
    static lv_point_t slpt[] = {{0, 0}, {120, 0}};
    lv_line_set_points(sline, slpt, 2);
    lv_obj_add_style(sline, &linestyle, 0);
    lv_obj_align(sline, LV_ALIGN_TOP_MID, 0, 10);

    lv_group_t *g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(indev, g);

    curr = lv_bar_create(lv_scr_act());
    maxa = lv_bar_create(lv_scr_act());
    currval = lv_label_create(lv_scr_act());
    lv_label_set_text(currval, "0");
    lv_obj_align(currval, LV_ALIGN_RIGHT_MID, 0, -10);
    maxval = lv_label_create(lv_scr_act());
    lv_label_set_text(maxval, "0");
    lv_obj_align(currval, LV_ALIGN_RIGHT_MID, 0, 20);
    lv_obj_align(curr, LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(maxa, LV_ALIGN_CENTER, 0, 30);

    static lv_obj_t *maxl = lv_label_create(lv_scr_act());
    static lv_obj_t *currl = lv_label_create(lv_scr_act());
    lv_label_set_text(maxl, "Max");
    lv_label_set_text(currl, "Current");
    lv_obj_align(currval, LV_ALIGN_LEFT_MID, 0, -10);
    lv_obj_align(currval, LV_ALIGN_LEFT_MID, 0, 20);

    /* slider = lv_spinbox_create(lv_scr_act()); */
    /*
    slider = lv_slider_create(lv_scr_act());
    lv_obj_set_size(slider, 120, 10);
    lv_obj_center(slider);

    lv_obj_t *sw1 = lv_switch_create(lv_scr_act());
    lv_obj_set_size(sw1, 40, 20);
    lv_obj_align(sw1, LV_ALIGN_BOTTOM_MID, -40, 0);
    lv_obj_t *sw2 = lv_switch_create(lv_scr_act());
    lv_obj_set_size(sw2, 40, 20);
    lv_obj_align(sw2, LV_ALIGN_BOTTOM_MID, 40, 0);
    */

    /* lv_obj_set_size(slider, 100, 10); */
    // lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, 0);
    /* lv_obj_add_event_cb(slider, slider_event_handler, LV_EVENT_VALUE_CHANGED,
     */
    /* NULL); */
    /*
    label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Bottom.");
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0);
    */
    lv_timer_create(update_uptime, 500, NULL);

    LV_LOG_INFO("Setup done.");
}

bool first = true;

void loop(void) {
    if (first) {
        LV_LOG_INFO("Loop start.");
    }
    /*
    oled.setFont(u8g2_font_ncenB14_tr);
    oled.drawStr(0, 20, "Hello!");
    oled.sendBuffer();
    delay(1000);
    */
    lv_task_handler();
    delay(1);
    if (first) {
        LV_LOG_INFO("Loop done.");
    }
    first = false;
}

