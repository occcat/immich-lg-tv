#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void app_load(App *a, bool reset) {
    if (a->video)
        media_stop();
    a->video = false;
    a->viewer = false;
    a->slideshow = false;
    a->loading = true;
    a->exit_confirm = false;
    a->error[0] = 0;
    a->focus_valid = false;
    /* A new route can have a different catalog variant. Do not let the loading
       frame interpret a previous asset catalog as an album catalog (or vice versa). */
    catalog_free(a->catalog);
    a->catalog = NULL;
    worker_cancel_pending();
    ++a->generation;
    ui_images_clear(a);
    if (reset) {
        for (int i = 0; i < a->page_count; i++)
            free(a->cursors[i]);
        free(a->cursors);
        free(a->pages);
        a->cursors = NULL;
        a->pages = NULL;
        a->page_count = 0;
        a->page = 1;
        a->selected = 0;
        a->scroll_row = 0;
    }
    Job j = {.kind = JOB_CATALOG,
             .generation = a->generation,
             .config = a->config,
             .view = a->view,
             .page = a->page};
    copy_text(j.album, sizeof j.album, a->album);
    if (a->page > 1 && a->page - 2 < a->page_count) {
        copy_text(j.cursor, sizeof j.cursor, a->cursors[a->page - 2]);
        j.page = a->pages[a->page - 2];
    }
    if (!worker_submit(&j, true)) {
        a->loading = false;
        copy_text(a->error, sizeof a->error, "任务队列已满，请重试");
    }
}

void app_results(App *a) {
    Result *r;
    while ((r = worker_poll())) {
        if (r->job.generation != a->generation) {
            result_free(r);
            continue;
        }
        if (r->job.kind == JOB_CATALOG) {
            a->loading = false;
            if (r->catalog) {
                catalog_free(a->catalog);
                a->catalog = r->catalog;
                r->catalog = NULL;
                a->connected = true;
                a->selected = 0;
                a->scroll_row = 0;
                if (a->config.remember) {
                    char e[ERROR_SIZE] = {0};
                    if (!config_save(a->config_path, &a->config, e))
                        copy_text(a->notice, sizeof a->notice, e);
                } else
                    config_forget(a->config_path);
            } else
                copy_text(a->error, sizeof a->error, r->error[0] ? r->error : "加载失败，请重试");
        } else {
            for (int i = 0; i < IMAGE_CACHE; i++) {
                ImageSlot *s = &a->images[i];
                if (!strcmp(s->id, r->job.id) && s->preview == r->job.preview) {
                    s->pending = false;
                    s->failed = !r->surface;
                    if (r->surface) {
                        s->texture = SDL_CreateTextureFromSurface(a->renderer, r->surface);
                        s->w = r->surface->w;
                        s->h = r->surface->h;
                    }
                    break;
                }
            }
        }
        result_free(r);
    }
}

int app_count(App *a) {
    return a->catalog ? (int)a->catalog->count : 0;
}

bool app_albums(App *a) {
    return a->view == VIEW_ALBUMS && !*a->album;
}

void app_next_page(App *a, int direction) {
    if (!a->catalog || a->loading || app_albums(a))
        return;
    if (direction < 0) {
        if (a->page > 1) {
            --a->page;
            app_load(a, false);
        }
        return;
    }
    if (!*a->catalog->next_cursor && !a->catalog->next_page)
        return;
    if (a->page > a->page_count) {
        char **c = realloc(a->cursors, (size_t)(a->page_count + 1) * sizeof *c);
        if (!c)
            return;
        a->cursors = c;
        int *p = realloc(a->pages, (size_t)(a->page_count + 1) * sizeof *p);
        if (!p)
            return;
        a->pages = p;
        a->cursors[a->page_count] = strdup(a->catalog->next_cursor);
        a->pages[a->page_count] = a->catalog->next_page ? a->catalog->next_page : a->page + 1;
        ++a->page_count;
    }
    ++a->page;
    app_load(a, false);
}
