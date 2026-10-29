#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void ui_draw_settings(App *a) {
    ui_draw_header(a, "连接设置", "连接你的 Immich，在大屏上回看自己的照片。");
    ui_round(a, 304, 268, 1000, 690, 20, (SDL_Color){15, 23, 37, 255});
    const char *labels[] = {"服务器地址", "只读 API 密钥"};
    for (int i = 0; i < 2; i++) {
        SDL_Rect r = ui_setting_rect(i);
        bool focus = a->setting_focus == i;
        paint_round(a, r, 12, focus ? (SDL_Color){31, 48, 77, 255} : (SDL_Color){23, 34, 51, 255});
        if (focus)
            ui_outline(a, r, UI_BLUE, 2);
        ui_label_box(a, labels[i], (SDL_Rect){r.x + 20, r.y + 12, r.w - 40, 26}, 0, UI_MUTED, 0);
        char masked[160];
        const char *value = i ? a->editing.api_key : a->editing.server;
        if (i && *value) {
            size_t n = strlen(value);
            if (n > 56)
                n = 56;
            memset(masked, '*', n);
            masked[n] = 0;
            value = masked;
        }
        ui_label_box(a, *value ? value : i ? "输入你的只读 API 密钥" : "https://photos.example.com",
                 (SDL_Rect){r.x + 20, r.y + 46, r.w - 40, 40}, 2, *value ? UI_WHITE : UI_MUTED, 0);
    }
    const char *titles[] = {"允许 HTTP 连接", "在本机记住连接"};
    const char *details[] = {"仅在可信的局域网中开启", "下次打开时，自动进入你的图库"};
    bool enabled[] = {a->editing.allow_http, a->editing.remember};
    for (int i = 0; i < 2; i++) {
        SDL_Rect r = ui_setting_rect(i + 2);
        if (a->setting_focus == i + 2)
            paint_round(a, r, 12, (SDL_Color){30, 44, 66, 255});
        ui_label_box(a, titles[i], (SDL_Rect){r.x + 20, r.y + 4, 730, 32}, 1, UI_WHITE, 0);
        ui_label_box(a, details[i], (SDL_Rect){r.x + 20, r.y + 42, 730, 26}, 0, UI_MUTED, 0);
        ui_round(a, r.x + r.w - 80, r.y + 20, 60, 32, 16,
                  enabled[i] ? (SDL_Color){104, 156, 238, 255} : (SDL_Color){56, 67, 84, 255});
        ui_circle(a, r.x + r.w - (enabled[i] ? 36 : 64), r.y + 36, 12, UI_WHITE);
    }
    SDL_Rect connect = ui_setting_rect(4);
    paint_round(a, connect, 12, a->setting_focus == 4 ? (SDL_Color){179, 207, 255, 255}
                                                  : (SDL_Color){105, 156, 237, 255});
    ui_label_box(a, "连接图库", connect, 3, (SDL_Color){12, 24, 45, 255}, 1);
    ui_label_box(a, "只读访问照片、相册与视频", (SDL_Rect){336, 844, 936, 26}, 0, UI_MUTED, 1);
    SDL_Rect forget = ui_setting_rect(5);
    if (a->setting_focus == 5)
        paint_round(a, forget, 12, (SDL_Color){45, 31, 40, 255});
    ui_label_box(a, "清除本机连接", (SDL_Rect){356, 878, 260, 60}, 1,
             (SDL_Color){226, 158, 163, 255}, 0);
    ui_label_box(a, "仅移除此电视上的连接信息", (SDL_Rect){716, 878, 536, 60}, 0, UI_MUTED, 2);
    ui_round(a, 1360, 268, 480, 444, 20, (SDL_Color){20, 30, 46, 255});
    ui_logo(a, 1416, 324, 24);
    ui_label_box(a, "让回忆，回到眼前。", (SDL_Rect){1392, 376, 416, 48}, 3, UI_WHITE, 0);
    ui_label_box(a, "在 Immich 账户设置中创建", (SDL_Rect){1392, 458, 416, 36}, 1, UI_MUTED, 0);
    ui_label_box(a, "只读 API 密钥，填入左侧。", (SDL_Rect){1392, 502, 416, 36}, 1, UI_MUTED, 0);
    ui_fill(a, 1392, 574, 416, 1, (SDL_Color){42, 54, 73, 255});
    ui_label_box(a, "照片留在你的服务器。", (SDL_Rect){1392, 610, 416, 36}, 1,
             (SDL_Color){180, 197, 219, 255}, 0);
    SDL_Rect interval = ui_setting_rect(6);
    paint_round(a, interval, 16, (SDL_Color){20, 30, 46, 255});
    if (a->setting_focus == 6)
        paint_ring(a, interval, 16, 2, UI_BLUE);
    ui_label_box(a, "幻灯片间隔", (SDL_Rect){1392, 778, 250, 38}, 1, UI_WHITE, 0);
    char value[96];
    snprintf(value, sizeof value, "%d 秒", a->editing.slideshow_seconds);
    ui_label_box(a, value, (SDL_Rect){1680, 776, 128, 42}, 3, UI_WHITE, 2);
    ui_label_box(a, "按 OK 切换播放间隔", (SDL_Rect){1392, 824, 416, 28}, 0, UI_MUTED, 0);
    if (*a->error || *a->notice)
        ui_label_box(a, *a->error ? a->error : a->notice, (SDL_Rect){1360, 890, 480, 70}, 0,
                 *a->error ? (SDL_Color){244, 160, 160, 255} : UI_BLUE, 0);
    ui_draw_footer(a, true);
}
