#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void ui_draw_sidebar(App *a) {
    ui_fill(a, 0, 0, 240, 1080, (SDL_Color){14, 21, 33, 255});
    ui_logo(a, 55, 82, 22);
    ui_label_box(a, "immich", (SDL_Rect){88, 54, 128, 56}, 4, UI_WHITE, 0);
    ui_label_box(a, "电视图库", (SDL_Rect){38, 120, 178, 32}, 0, UI_MUTED, 0);
    ui_label_box(a, "浏览", (SDL_Rect){40, 202, 140, 28}, 0, UI_MUTED, 0);
    for (int i = 0; i < 5; i++) {
        SDL_Rect r = ui_nav_rect(i);
        bool active = i == 4 ? a->settings : (a->view == (View)i && !a->settings);
        bool focused = a->sidebar && a->nav == i && !a->settings;
        if (active || focused)
            paint_round(a, r, 16, focused ? (SDL_Color){49, 69, 105, 255}
                                         : (SDL_Color){27, 40, 61, 255});
        if (focused)
            paint_ring(a, r, 16, 2, (SDL_Color){145, 180, 237, 255});
        ui_nav_icon(a, i, r.x + 18, r.y + 18, active || focused ? UI_BLUE : UI_MUTED);
        ui_label_box(a, UI_NAV_NAMES[i], (SDL_Rect){r.x + 68, r.y, 116, r.h}, 2,
                 active || focused ? UI_WHITE : UI_MUTED, 0);
    }
    ui_fill(a, 40, 776, 160, 1, (SDL_Color){34, 44, 61, 255});
    ui_circle(a, 42, 943, 4, a->connected ? (SDL_Color){114, 207, 164, 255} : UI_MUTED);
    ui_label_box(a, a->connected ? "图库已连接" : "尚未连接", (SDL_Rect){56, 928, 156, 30}, 0, UI_MUTED, 0);
    ui_label_box(a, "只读浏览", (SDL_Rect){39, 973, 124, 32}, 0,
             (SDL_Color){151, 207, 183, 255}, 0);
    ui_label_box(a, APP_VERSION, (SDL_Rect){160, 973, 48, 32}, 0, UI_MUTED, 2);
}

void ui_draw_header(App *a, const char *title, const char *subtitle) {
    ui_label_box(a, *a->album && !a->settings ? "相册 / 照片" : "我的图库",
             (SDL_Rect){304, 52, 1100, 30}, 0, UI_BLUE, 0);
    ui_label_box(a, title, (SDL_Rect){300, 98, 1280, 76}, 6, UI_WHITE, 0);
    ui_label_box(a, subtitle, (SDL_Rect){304, 188, 1280, 32}, 1, UI_MUTED, 0);
    time_t t = time(NULL);
    struct tm clock;
    localtime_r(&t, &clock);
    char label[64];
    strftime(label, sizeof label, "%H:%M", &clock);
    ui_label_box(a, label, (SDL_Rect){1650, 60, 190, 62}, 5, UI_WHITE, 2);
    snprintf(label, sizeof label, "%d 月 %d 日", clock.tm_mon + 1, clock.tm_mday);
    ui_label_box(a, label, (SDL_Rect){1620, 132, 220, 36}, 1, UI_MUTED, 2);
}

int ui_key_hint(App *a, int x, const char *key, const char *label, int key_width, int width) {
    ui_round(a, x, 1020, key_width, 34, 8, (SDL_Color){29, 40, 57, 255});
    ui_label_box(a, key, (SDL_Rect){x, 1020, key_width, 34}, 0, UI_WHITE, 1);
    ui_label_box(a, label, (SDL_Rect){x + key_width + 12, 1020, width, 34}, 0, UI_MUTED, 0);
    return x + key_width + width + 44;
}

void ui_draw_footer(App *a, bool settings) {
    ui_fill(a, 304, 995, 1536, 1, (SDL_Color){30, 40, 56, 255});
    int x = ui_key_hint(a, 304, "方向键", settings ? "选择" : "浏览", 84, 72);
    x = ui_key_hint(a, x, "OK", settings ? "编辑 / 切换" : "打开", 50, settings ? 130 : 72);
    ui_key_hint(a, x, "返回", settings ? "回到图库" : "导航", 64, 112);
    if (!settings && !app_albums(a))
        ui_key_hint(a, 1608, "CH ±", "翻页", 72, 72);
}
