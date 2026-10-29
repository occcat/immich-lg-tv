#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *UI_SUBTITLES[] = {"按拍摄时间，重温生活里的每一刻。",
    "把一段旅程、一个周末，慢慢看完。", "那些想再看一次的瞬间。", "让回忆，重新动起来。"};

void ui_draw_gallery(App *a, uint32_t now) {
    ui_draw_header(a, *a->album ? a->album_name : UI_NAV_NAMES[a->view],
                *a->album ? "一起回看，相册里的每一段故事。" : UI_SUBTITLES[a->view]);
    GridLayout g = ui_grid(a);
    const char *section = *a->album ? "相册中的照片" : app_albums(a) ? "全部相册"
                          : a->view == VIEW_FAVORITES ? "我的收藏"
                          : a->view == VIEW_VIDEOS ? "全部视频" : "最近拍摄";
    ui_label_box(a, section, (SDL_Rect){304, 252, 1050, 40}, 2, UI_WHITE, 0);
    char label[320];
    if (app_albums(a))
        snprintf(label, sizeof label, "%zu 个相册", a->catalog ? a->catalog->count : 0);
    else
        snprintf(label, sizeof label, "第 %d 页  /  %zu 项", a->page,
                 a->catalog ? a->catalog->count : 0);
    ui_label_box(a, label, (SDL_Rect){1450, 252, 390, 40}, 1, UI_MUTED, 2);
    ui_draw_footer(a, false);
    if (a->loading) {
        for (int i = 0; i < g.columns * g.rows; i++) {
            SDL_Rect r = ui_card(g, i);
            paint_round(a, r, 14, (SDL_Color){22 + (Uint8)((now / 70 + i) % 4), 31, 46, 255});
            ui_round(a, r.x, r.y + r.h + 14, 124, 18, 6, (SDL_Color){23, 32, 46, 255});
        }
        return;
    }
    if (*a->error || !app_count(a)) {
        paint_icon(a, *a->error ? 4 : app_albums(a) ? 1 : 0, (SDL_Rect){1028, 484, 88, 88}, UI_MUTED);
        ui_label_box(a, *a->error ? "暂时无法读取图库" : "这里还没有内容",
                 (SDL_Rect){464, 616, 1216, 70}, 4, UI_WHITE, 1);
        ui_label_box(a, *a->error ? a->error : "换个分类看看，或在 Immich 中添加照片。",
                 (SDL_Rect){464, 704, 1216, 48}, 1, UI_MUTED, 1);
        if (*a->error)
            ui_label_box(a, "按 OK 重试，按返回键检查连接。", (SDL_Rect){464, 774, 1216, 48}, 1, UI_BLUE, 1);
        return;
    }
    for (int i = 0; i < g.columns * g.rows; i++) {
        int index = a->scroll_row * g.columns + i;
        if (index >= app_count(a))
            break;
        SDL_Rect box = ui_card(g, i);
        int x = box.x, y = box.y;
        paint_round(a, box, 14, (SDL_Color){26, 36, 52, 255});
        const char *id = app_albums(a) ? a->catalog->albums[index].cover_id
                                  : a->catalog->assets[index].id;
        ImageSlot *image = ui_image_get(a, id, false);
        ui_image_draw(a, image, box, false);
        paint_clip_corners(a, box, 14, UI_BG);
        if (!valid_asset_id(id) && app_albums(a))
            paint_icon(a, 1, (SDL_Rect){x + (box.w - 72) / 2, y + (box.h - 72) / 2, 72, 72},
                       (SDL_Color){100, 117, 142, 255});
        if (image && image->failed)
            ui_label_box(a, "缩略图不可用", box, 0, UI_MUTED, 1);
        bool selected = !a->sidebar && index == a->selected;
        if (selected) {
            if (!a->focus_valid) {
                a->focus_x = x;
                a->focus_y = y;
                a->focus_valid = true;
            }
            a->focus_x += (x - a->focus_x) * .35f;
            a->focus_y += (y - a->focus_y) * .35f;
            paint_ring(a, (SDL_Rect){(int)a->focus_x - 6, (int)a->focus_y - 6, box.w + 12, box.h + 12},
                       20, 3, (SDL_Color){170, 201, 255, 255});
        }
        SDL_Color caption = selected ? UI_WHITE : (SDL_Color){177, 190, 210, 255};
        if (app_albums(a)) {
            Album *b = &a->catalog->albums[index];
            ui_label_box(a, b->name, (SDL_Rect){x, y + box.h + 12, box.w - 114, 44}, 2, caption, 0);
            snprintf(label, sizeof label, "%d 张", b->count);
            ui_label_box(a, label, (SDL_Rect){x + box.w - 100, y + box.h + 12, 100, 44}, 1, UI_MUTED, 2);
        } else {
            Asset *b = &a->catalog->assets[index];
            if (b->video) {
                snprintf(label, sizeof label, "%d:%02d", (int)b->duration / 60, (int)b->duration % 60);
                TextSlot *duration = ui_text_slot(a, label, 0, UI_WHITE);
                int label_width = duration ? duration->ink.w : 42;
                ui_round(a, x + 12, y + box.h - 46, label_width + 38, 34, 9,
                          (SDL_Color){0, 0, 0, 180});
                paint_icon(a, 3, (SDL_Rect){x + 21, y + box.h - 36, 14, 14}, UI_WHITE);
                ui_label_box(a, label, (SDL_Rect){x + 40, y + box.h - 46, label_width, 34}, 0, UI_WHITE, 1);
            }
            if (b->favorite) {
                ui_round(a, x + box.w - 46, y + 12, 34, 34, 17, (SDL_Color){4, 10, 18, 155});
                paint_icon(a, 5, (SDL_Rect){x + box.w - 39, y + 19, 20, 20}, UI_WHITE);
            }
            if (a->view == VIEW_VIDEOS)
                copy_text(label, sizeof label, b->name);
            else if (*b->city)
                copy_text(label, sizeof label, b->city);
            else
                snprintf(label, sizeof label, "%.10s", b->date);
            ui_label_box(a, label, (SDL_Rect){x, y + box.h + 12, box.w - 88, 40},
                     a->view == VIEW_VIDEOS ? 2 : 1, caption, 0);
            if (strlen(b->date) >= 10)
                snprintf(label, sizeof label, "%.2s.%.2s", b->date + 5, b->date + 8);
            else
                label[0] = 0;
            ui_label_box(a, label, (SDL_Rect){x + box.w - 80, y + box.h + 12, 80, 40}, 0, UI_MUTED, 2);
        }
    }
    /* Visible range makes scroll position clear without inventing a total count
       beyond the current API page. */
    if (app_count(a) > g.columns * g.rows) {
        int visible = g.columns * g.rows;
        int last = a->scroll_row * g.columns + visible;
        if (last > app_count(a))
            last = app_count(a);
        snprintf(label, sizeof label, "%d – %d / %d", a->scroll_row * g.columns + 1, last, app_count(a));
        ui_label_box(a, label, (SDL_Rect){1300, 1020, 260, 34}, 0, UI_MUTED, 2);
    }
}
