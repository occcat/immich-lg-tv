#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *const UI_KEYS_LOWER = "1234567890qwertyuiopasdfghjkl:zxcvbnm./-@_?=&%[]+#!$~*,";
const char *const UI_KEYS_UPPER = "1234567890QWERTYUIOPASDFGHJKL:ZXCVBNM./-@_?=&%[]+#!$~*,";

void ui_draw_keyboard(App *a) {
    ui_fill(a, 0, 0, 1920, 1080, (SDL_Color){0, 0, 0, 195});
    ui_round(a, 390, 220, 1140, 720, 18, (SDL_Color){18, 28, 45, 255});
    ui_label(a, a->keyboard_field ? "输入 API 密钥" : "输入服务器地址", 430, 250, 4, UI_WHITE, 1060);
    const char *value = a->keyboard_field ? a->editing.api_key : a->editing.server;
    char masked[560];
    if (a->keyboard_field) {
        size_t n = strlen(value);
        memset(masked, '*', n);
        masked[n] = 0;
        value = masked;
    }
    ui_round(a, 430, 331, 1060, 79, 9, (SDL_Color){9, 17, 30, 255});
    ui_label_box(a, value, (SDL_Rect){452, 331, 1010, 79}, 2, UI_WHITE, 0);
    const char *k = a->shift ? UI_KEYS_UPPER : UI_KEYS_LOWER;
    for (int i = 0; i < 60; i++) {
        int x = 430 + i % 10 * 106, y = 444 + i / 10 * 76;
        ui_round(a, x, y, 96, 66, 8,
                  i == a->keyboard_index ? (SDL_Color){139, 179, 245, 255}
                                         : (SDL_Color){35, 49, 71, 255});
        char letter[2] = {0};
        const char *s = "";
        if (i < (int)strlen(k)) {
            letter[0] = k[i];
            s = letter;
        } else if (i == 55)
            s = "Aa";
        else if (i == 56)
            s = "删除";
        else if (i == 57)
            s = "清空";
        else if (i == 58)
            s = "完成";
        else if (i == 59)
            s = "取消";
        ui_label_box(a, s, (SDL_Rect){x, y, 96, 66}, 2, i == a->keyboard_index ? UI_BG : UI_WHITE, 1);
    }
    ui_label(a, "支持遥控器、魔法指针或实体键盘。返回键取消本次编辑。", 430, 908, 0, UI_MUTED, 1060);
}
