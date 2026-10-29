#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void ui_draw(App *a, uint32_t now) {
    ++a->frames;
    SDL_SetRenderDrawColor(a->renderer, UI_BG.r, UI_BG.g, UI_BG.b, 255);
    SDL_RenderClear(a->renderer);
    if (a->viewer && a->catalog && !app_albums(a) && app_count(a) > 0)
        ui_draw_viewer(a, now);
    else {
        ui_draw_sidebar(a);
        if (a->settings)
            ui_draw_settings(a);
        else
            ui_draw_gallery(a, now);
    }
    if (a->keyboard)
        ui_draw_keyboard(a);
    if (*a->error && a->viewer && !a->video) {
        ui_round(a, 300, 70, 1320, 75, 12, (SDL_Color){67, 34, 43, 240});
        ui_label_box(a, a->error, (SDL_Rect){330, 70, 1260, 75}, 1, UI_WHITE, 1);
    }
    if (a->exit_confirm) {
        ui_fill(a, 0, 0, 1920, 1080, (SDL_Color){0, 0, 0, 185});
        ui_round(a, 580, 360, 760, 340, 20, (SDL_Color){20, 31, 48, 255});
        ui_label_box(a, "离开放映室？", (SDL_Rect){610, 400, 700, 90}, 5, UI_WHITE, 1);
        ui_label_box(a, "按 OK 退出，按返回键继续浏览。", (SDL_Rect){610, 516, 700, 65}, 2, UI_MUTED, 1);
    }
}
