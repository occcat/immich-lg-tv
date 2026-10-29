#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void app_viewer_change(App *a, int direction) {
    if (!app_count(a))
        return;
    int n = a->selected + direction;
    if (n < 0 || n >= app_count(a)) {
        a->slideshow = false;
        return;
    }
    if (a->video)
        media_stop();
    a->video = false;
    a->paused = false;
    a->selected = n;
    a->zoom = 1;
    ui_image_get(a, a->catalog->assets[n].id, true);
    a->last_slide = SDL_GetTicks();
}

void app_open_selected(App *a) {
    if (a->loading || !a->catalog || a->selected < 0 || a->selected >= app_count(a))
        return;
    if (app_albums(a)) {
        Album *b = &a->catalog->albums[a->selected];
        copy_text(a->album, sizeof a->album, b->id);
        copy_text(a->album_name, sizeof a->album_name, b->name);
        app_load(a, true);
        a->sidebar = false;
        return;
    }
    a->viewer = true;
    a->error[0] = 0;
    a->info = false;
    a->zoom = 1;
    a->paused = false;
    a->last_input = SDL_GetTicks();
    ui_image_get(a, a->catalog->assets[a->selected].id, true);
}

void app_back(App *a) {
    if (a->keyboard) {
        char *dst = a->keyboard_field == 0 ? a->editing.server : a->editing.api_key;
        size_t cap = a->keyboard_field == 0 ? sizeof a->editing.server : sizeof a->editing.api_key;
        copy_text(dst, cap, a->keyboard_before);
        a->keyboard = false;
        SDL_StopTextInput();
        return;
    }
    if (a->exit_confirm) {
        a->exit_confirm = false;
        return;
    }
    if (a->video) {
        media_stop();
        a->video = false;
        a->paused = false;
        return;
    }
    if (a->viewer) {
        a->viewer = false;
        a->slideshow = false;
        a->info = false;
        a->zoom = 1;
        GridLayout g = ui_grid(a);
        int row = a->selected / g.columns;
        if (row < a->scroll_row)
            a->scroll_row = row;
        if (row >= a->scroll_row + g.rows)
            a->scroll_row = row - g.rows + 1;
        a->focus_valid = false;
        return;
    }
    if (a->settings) {
        if (a->connected) {
            a->settings = false;
            a->sidebar = true;
        } else
            a->exit_confirm = true;
        return;
    }
    if (*a->album) {
        a->album[0] = a->album_name[0] = 0;
        app_load(a, true);
        a->sidebar = false;
        return;
    }
    if (!a->sidebar) {
        a->sidebar = true;
        return;
    }
    a->exit_confirm = true;
}

void app_tick(App *a, uint32_t now) {
    app_results(a);
    if (a->viewer && a->slideshow && !a->video &&
        now - a->last_slide > (uint32_t)a->config.slideshow_seconds * 1000) {
        int next = a->selected + 1;
        while (next < app_count(a) && a->catalog->assets[next].video)
            ++next;
        if (next >= app_count(a)) {
            a->slideshow = false;
            return;
        }
        a->selected = next;
        a->last_slide = now;
        a->zoom = 1;
        ui_image_get(a, a->catalog->assets[next].id, true);
    }
    if (a->video) {
        int state = media_state();
        if (state == 2) {
            media_stop();
            a->video = false;
        } else if (state == 0 && now - a->last_slide > 20000) {
            media_stop();
            a->video = false;
            copy_text(a->error, sizeof a->error, "视频启动超时，请检查服务端转码结果");
        }
    }
}
