#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void ui_draw_viewer(App *a, uint32_t now) {
    Asset *b = &a->catalog->assets[a->selected];
    if (a->video) {
        SDL_SetRenderDrawColor(a->renderer, 0, 0, 0, 0);
        SDL_RenderClear(a->renderer);
        FrameRect frame = fit_frame(b->width, b->height, 1920, 1080);
        SDL_Color black = {0, 0, 0, 255};
        if (frame.x > 0) {
            ui_fill(a, 0, 0, frame.x, 1080, black);
            ui_fill(a, frame.x + frame.w, 0, 1920 - frame.x - frame.w, 1080, black);
        }
        if (frame.y > 0) {
            ui_fill(a, 0, 0, 1920, frame.y, black);
            ui_fill(a, 0, frame.y + frame.h, 1920, 1080 - frame.y - frame.h, black);
        }
        int state = media_state();
        if (state < 0) {
            ui_label(a, "视频暂时无法播放", 90, 790, 5, UI_WHITE, 1700);
            ui_label(a, media_error(), 90, 872, 2, UI_MUTED, 1700);
        } else if (state == 0)
            ui_label(a, "正在准备视频…", 90, 872, 3, UI_WHITE, 1700);
        if (now - a->last_input < 5000 || a->paused || state < 0) {
            ui_fill(a, 0, 926, 1920, 154, (SDL_Color){4, 10, 18, 220});
            ui_label(a, b->name, 78, 944, 2, UI_WHITE, 1200);
            ui_label(a, a->paused ? "已暂停 · OK 继续" : "OK 暂停    ← / → 跳转 10 秒    返回 停止播放",
                 78, 1002, 1, UI_MUTED, 1700);
        }
        return;
    }
    ui_fill(a, 0, 0, 1920, 1080, (SDL_Color){3, 6, 10, 255});
    ImageSlot *slot = ui_image_get(a, b->id, true);
    if (slot && slot->texture) {
        int w = (int)(1920 * a->zoom), h = (int)(1080 * a->zoom);
        ui_image_draw(a, slot, (SDL_Rect){(1920 - w) / 2, (1080 - h) / 2, w, h}, true);
    } else
        ui_label_box(a, slot && slot->failed ? "这张照片暂时无法显示" : "正在载入清晰照片…",
                 (SDL_Rect){0, 0, 1920, 1080}, 2, UI_MUTED, 1);
    if (a->info || now - a->last_input < 4500 || a->slideshow || b->video) {
        SDL_Rect shade_rect = {0, 800, 1920, 280};
        SDL_RenderCopy(a->renderer, a->shade_vertical, NULL, &shade_rect);
        char label[480];
        snprintf(label, sizeof label, "%d / %d    %.10s%s%s", a->selected + 1, app_count(a), b->date,
                 *b->city ? "  ·  " : "", b->city);
        ui_label(a, label, 74, 885, 1, (SDL_Color){215, 225, 239, 255}, 1700);
        ui_label(a, b->name, 71, 930, 3, UI_WHITE, 1600);
        const char *hint =
            b->video ? "OK  播放视频     ← →  切换     ↑  信息     返回  图库"
            : a->slideshow
                ? "幻灯片播放中    OK 暂停     ← → 切换     返回 图库"
                : "← →  切换     OK  幻灯片     ↑  信息     ↓  放大 / 还原     返回  图库";
        ui_label(a, hint, 74, 1017, 0, (SDL_Color){179, 195, 217, 255}, 1750);
    }
    if (a->info) {
        ui_round(a, 1330, 75, 510, 270, 16, (SDL_Color){15, 24, 39, 235});
        ui_label(a, "照片信息", 1360, 96, 3, UI_WHITE, 450);
        char s[160];
        snprintf(s, sizeof s, "%d × %d%s", b->width, b->height, b->video ? "  ·  视频" : "");
        ui_label(a, s, 1360, 164, 1, UI_MUTED, 450);
        ui_label(a, b->camera, 1360, 213, 1, UI_MUTED, 450);
        ui_label(a, b->favorite ? "已在 Immich 收藏" : "", 1360, 262, 1, UI_BLUE, 450);
    }
}
