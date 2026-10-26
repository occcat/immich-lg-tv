#include "ui.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const SDL_Color WHITE = {241, 244, 250, 255}, MUTED = {143, 157, 178, 255},
                       BLUE = {133, 174, 255, 255}, BG = {10, 15, 24, 255};
static const int sizes[] = {18, 22, 26, 30, 36, 44, 56, 72};
static const char *names[] = {"照片", "相册", "收藏", "视频", "连接设置"};
static const char *subtitles[] = {"那些平凡的日子，也值得好好收藏。",
                                  "把每一段故事，放进它的相册。", "你曾经标记的喜欢，都在这里。",
                                  "让会动的回忆，再播放一遍。"};
static const char *keys_lower = "1234567890qwertyuiopasdfghjkl:zxcvbnm./-@_?=&%[]+#!$~*,";
static const char *keys_upper = "1234567890QWERTYUIOPASDFGHJKL:ZXCVBNM./-@_?=&%[]+#!$~*,";

static void fill(App *a, int x, int y, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawColor(a->renderer, color.r, color.g, color.b, color.a);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(a->renderer, &r);
}
static void round_box(App *a, int x, int y, int w, int h, int radius, SDL_Color color) {
    paint_round(a, (SDL_Rect){x, y, w, h}, radius, color);
}
static void outline(App *a, SDL_Rect r, SDL_Color c, int width) {
    paint_ring(a, r, r.w > 500 ? 12 : 20, width, c);
}
static TextSlot *text_slot(App *a, const char *s, int size, SDL_Color c) {
    if (!s || !*s)
        return NULL;
    TextSlot *slot = NULL;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < TEXT_CACHE; i++) {
        TextSlot *t = &a->text[i];
        if (t->texture && t->size == size && !memcmp(&t->color, &c, sizeof c) &&
            !strcmp(t->text, s)) {
            t->used = a->frames;
            return t;
        }
        if (!t->texture) {
            slot = t;
            oldest = 0;
        } else if (oldest && t->used < oldest) {
            oldest = t->used;
            slot = t;
        }
    }
    if (!slot)
        return NULL;
    if (slot->texture)
        SDL_DestroyTexture(slot->texture);
    SDL_Surface *surface = TTF_RenderUTF8_Blended(a->fonts[size], s, c);
    memset(slot, 0, sizeof *slot);
    if (!surface)
        return NULL;
    slot->texture = SDL_CreateTextureFromSurface(a->renderer, surface);
    slot->w = surface->w;
    slot->h = surface->h;
    int left = surface->w, top = surface->h, right = -1, bottom = -1;
    for (int y = 0; y < surface->h; ++y)
        for (int x = 0; x < surface->w; ++x) {
            Uint8 red, green, blue, alpha;
            Uint32 pixel =
                *(Uint32 *)((unsigned char *)surface->pixels + y * surface->pitch + x * 4);
            SDL_GetRGBA(pixel, surface->format, &red, &green, &blue, &alpha);
            if (alpha >= 8) {
                if (x < left)
                    left = x;
                if (x > right)
                    right = x;
                if (y < top)
                    top = y;
                if (y > bottom)
                    bottom = y;
            }
        }
    slot->ink = (SDL_Rect){left, top, right - left + 1, bottom - top + 1};
    if (right < left)
        slot->ink = (SDL_Rect){0, 0, surface->w, surface->h};
    SDL_FreeSurface(surface);
    copy_text(slot->text, sizeof slot->text, s);
    slot->size = size;
    slot->color = c;
    slot->used = a->frames;
    return slot;
}
static void text(App *a, const char *s, int x, int y, int size, SDL_Color c, int max_width) {
    TextSlot *t = text_slot(a, s, size, c);
    if (!t || !t->texture)
        return;
    SDL_Rect dest = {x, y, t->w, t->h}, src = {0, 0, t->w, t->h};
    if (max_width > 0 && dest.w > max_width) {
        dest.w = src.w = max_width;
    }
    SDL_RenderCopy(a->renderer, t->texture, &src, &dest);
}
/* Align visible glyphs, not the font's ascent/descent padding. Alignment is
   shared by buttons, badges, navigation, fields, captions and keyboard keys. */
static void text_box(App *a, const char *s, SDL_Rect box, int size, SDL_Color c, int align) {
    TextSlot *t = text_slot(a, s, size, c);
    if (!t || !t->texture)
        return;
    SDL_Rect source = t->ink;
    if (source.w > box.w)
        source.w = box.w;
    int x = box.x + (align == 1 ? (box.w - source.w) / 2 : align == 2 ? box.w - source.w : 0);
    SDL_Rect dest = {x, box.y + (box.h - source.h) / 2, source.w, source.h};
    SDL_RenderCopy(a->renderer, t->texture, &source, &dest);
}
static void image_draw(App *a, ImageSlot *slot, SDL_Rect dst, bool contain) {
    if (!slot || !slot->texture)
        return;
    double ratio = (double)slot->w / slot->h, box = (double)dst.w / dst.h;
    SDL_Rect src = {0, 0, slot->w, slot->h};
    if (contain) {
        if (ratio > box) {
            int h = (int)(dst.w / ratio);
            dst.y += (dst.h - h) / 2;
            dst.h = h;
        } else {
            int w = (int)(dst.h * ratio);
            dst.x += (dst.w - w) / 2;
            dst.w = w;
        }
    } else if (ratio > box) {
        src.w = (int)(slot->h * box);
        src.x = (slot->w - src.w) / 2;
    } else {
        src.h = (int)(slot->w / box);
        src.y = (slot->h - src.h) / 2;
    }
    SDL_RenderCopy(a->renderer, slot->texture, &src, &dst);
}
static ImageSlot *get_image(App *a, const char *id, bool preview) {
    if (!valid_asset_id(id))
        return NULL;
    ImageSlot *slot = NULL;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < IMAGE_CACHE; i++) {
        ImageSlot *s = &a->images[i];
        if (!strcmp(s->id, id) && s->preview == preview) {
            s->used = a->frames;
            return s;
        }
        if (!*s->id) {
            slot = s;
            oldest = 0;
        } else if (oldest && s->used < oldest && !s->pending) {
            slot = s;
            oldest = s->used;
        }
    }
    if (!slot)
        return NULL;
    if (slot->texture)
        SDL_DestroyTexture(slot->texture);
    memset(slot, 0, sizeof *slot);
    copy_text(slot->id, sizeof slot->id, id);
    slot->preview = preview;
    slot->used = a->frames;
    Job job = {
        .kind = JOB_IMAGE, .generation = a->generation, .config = a->config, .preview = preview};
    copy_text(job.id, sizeof job.id, id);
    slot->pending = worker_submit(&job, preview);
    return slot;
}
static void clear_images(App *a) {
    for (int i = 0; i < IMAGE_CACHE; i++) {
        if (a->images[i].texture)
            SDL_DestroyTexture(a->images[i].texture);
        memset(&a->images[i], 0, sizeof a->images[i]);
    }
}
static void circle(App *a, int x, int y, int r, SDL_Color c) {
    paint_round(a, (SDL_Rect){x - r, y - r, 2 * r, 2 * r}, r, c);
}
static void logo(App *a, int x, int y, int r) {
    paint_icon(a, 6, (SDL_Rect){x - r, y - r, 2 * r, 2 * r}, (SDL_Color){255, 255, 255, 255});
}
static void nav_icon(App *a, int i, int x, int y, SDL_Color c) {
    paint_icon(a, i, (SDL_Rect){x, y, 32, 32}, c);
}

bool ui_init(App *a, const char *assets, bool hidden, char *error) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
#ifdef WEBOS
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) || TTF_Init() < 0) {
        copy_text(error, ERROR_SIZE, "无法初始化电视图形界面");
        return false;
    }
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP);
    Uint32 flags = hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
#ifdef WEBOS
    flags |= SDL_WINDOW_OPENGL;
#else
    flags |= SDL_WINDOW_RESIZABLE;
#endif
    a->window = SDL_CreateWindow("Immich TV", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
#ifdef WEBOS
                                 1920, 1080,
#else
                                 1280, 720,
#endif
                                 flags);
    if (!a->window)
        return false;
    a->renderer =
        SDL_CreateRenderer(a->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!a->renderer)
        a->renderer = SDL_CreateRenderer(a->window, -1, SDL_RENDERER_SOFTWARE);
    if (!a->renderer)
        return false;
    SDL_RenderSetLogicalSize(a->renderer, 1920, 1080);
    SDL_SetRenderDrawBlendMode(a->renderer, SDL_BLENDMODE_BLEND);
    SDL_Surface *shade = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1, 32, SDL_PIXELFORMAT_RGBA32);
    if (!shade)
        return false;
    for (int x = 0; x < 1920; ++x)
        ((Uint32 *)shade->pixels)[x] =
            SDL_MapRGBA(shade->format, 8, 16, 28, (Uint8)(225.0 * (1.0 - x / 1919.0)));
    a->shade_horizontal = SDL_CreateTextureFromSurface(a->renderer, shade);
    SDL_FreeSurface(shade);
    shade = SDL_CreateRGBSurfaceWithFormat(0, 1, 280, 32, SDL_PIXELFORMAT_RGBA32);
    if (!shade)
        return false;
    for (int y = 0; y < 280; ++y)
        *(Uint32 *)((unsigned char *)shade->pixels + y * shade->pitch) =
            SDL_MapRGBA(shade->format, 3, 8, 15, (Uint8)(y * .78));
    a->shade_vertical = SDL_CreateTextureFromSurface(a->renderer, shade);
    SDL_FreeSurface(shade);
    char font[1200];
    snprintf(font, sizeof font, "%s/Interface.ttf", assets);
    for (int i = 0; i < 8; i++) {
        a->fonts[i] = TTF_OpenFont(font, sizes[i]);
        if (!a->fonts[i]) {
            copy_text(error, ERROR_SIZE, "无法加载界面字体");
            return false;
        }
        TTF_SetFontHinting(a->fonts[i], TTF_HINTING_LIGHT);
    }
    if (!paint_init(a, assets)) {
        copy_text(error, ERROR_SIZE, "无法加载矢量图标");
        return false;
    }
    a->sidebar = true;
    a->zoom = 1.0;
    a->selected = 0;
    a->page = 1;
    return true;
}
void ui_destroy(App *a) {
    media_shutdown();
    catalog_free(a->catalog);
    clear_images(a);
    for (int i = 0; i < TEXT_CACHE; i++)
        if (a->text[i].texture)
            SDL_DestroyTexture(a->text[i].texture);
    for (int i = 0; i < 8; i++)
        if (a->fonts[i])
            TTF_CloseFont(a->fonts[i]);
    for (int i = 0; i < a->page_count; ++i)
        free(a->cursors[i]);
    free(a->cursors);
    free(a->pages);
    if (a->shade_horizontal)
        SDL_DestroyTexture(a->shade_horizontal);
    if (a->shade_vertical)
        SDL_DestroyTexture(a->shade_vertical);
    paint_destroy(a);
    if (a->renderer)
        SDL_DestroyRenderer(a->renderer);
    if (a->window)
        SDL_DestroyWindow(a->window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
}

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
    clear_images(a);
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
static int count(App *a) {
    return a->catalog ? (int)a->catalog->count : 0;
}
static bool albums(App *a) {
    return a->view == VIEW_ALBUMS && !*a->album;
}
static void viewer_change(App *a, int direction) {
    if (!count(a))
        return;
    int n = a->selected + direction;
    if (n < 0 || n >= count(a)) {
        a->slideshow = false;
        return;
    }
    if (a->video)
        media_stop();
    a->video = false;
    a->paused = false;
    a->selected = n;
    a->zoom = 1;
    get_image(a, a->catalog->assets[n].id, true);
    a->last_slide = SDL_GetTicks();
}
void app_open_selected(App *a) {
    if (a->loading || !a->catalog || a->selected < 0 || a->selected >= count(a))
        return;
    if (albums(a)) {
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
    get_image(a, a->catalog->assets[a->selected].id, true);
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
        int row = a->selected / 5;
        if (row < a->scroll_row)
            a->scroll_row = row;
        if (row > a->scroll_row + 1)
            a->scroll_row = row - 1;
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
static void begin_edit(App *a, int field) {
    a->keyboard = true;
    a->keyboard_field = field;
    a->keyboard_index = 0;
    copy_text(a->keyboard_before, sizeof a->keyboard_before,
              field ? a->editing.api_key : a->editing.server);
    SDL_StartTextInput();
}
static void erase_character(char *s) {
    size_t n = strlen(s);
    if (!n)
        return;
    --n;
    while (n && ((unsigned char)s[n] & 0xc0) == 0x80)
        --n;
    s[n] = 0;
}
void ui_text(App *a, const char *s) {
    if (!a->keyboard)
        return;
    char *dst = a->keyboard_field ? a->editing.api_key : a->editing.server;
    size_t cap = a->keyboard_field ? sizeof a->editing.api_key : sizeof a->editing.server;
    size_t n = strlen(dst);
    if (n + strlen(s) < cap && !strpbrk(s, "\r\n"))
        strcat(dst, s);
}
static void setting_activate(App *a) {
    if (a->setting_focus < 2) {
        begin_edit(a, a->setting_focus);
        return;
    }
    if (a->setting_focus == 2)
        a->editing.allow_http = !a->editing.allow_http;
    if (a->setting_focus == 3)
        a->editing.remember = !a->editing.remember;
    if (a->setting_focus == 4) {
        char normalized[768];
        if (!normalize_server(a->editing.server, normalized, sizeof normalized, a->error))
            return;
        if (!*a->editing.api_key) {
            copy_text(a->error, sizeof a->error, "请填写 API 密钥");
            return;
        }
        copy_text(a->editing.server, sizeof a->editing.server, normalized);
        a->config = a->editing;
        a->settings = false;
        a->view = VIEW_TIMELINE;
        a->nav = 0;
        a->album[0] = 0;
        a->sidebar = false;
        app_load(a, true);
    }
    if (a->setting_focus == 5) {
        config_forget(a->config_path);
        memset(a->editing.api_key, 0, sizeof a->editing.api_key);
        memset(a->config.api_key, 0, sizeof a->config.api_key);
        catalog_free(a->catalog);
        a->catalog = NULL;
        a->connected = false;
        a->settings = true;
        worker_cancel_pending();
        ++a->generation;
        clear_images(a);
        copy_text(a->notice, sizeof a->notice, "本机保存的连接已清除");
    }
    if (a->setting_focus == 6) {
        a->editing.slideshow_seconds += 3;
        if (a->editing.slideshow_seconds > 20)
            a->editing.slideshow_seconds = 5;
        a->config.slideshow_seconds = a->editing.slideshow_seconds;
        if (a->config.remember) {
            char e[ERROR_SIZE];
            config_save(a->config_path, &a->config, e);
        }
    }
}
static void next_page(App *a, int direction) {
    if (!a->catalog || a->loading || albums(a))
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
void ui_key(App *a, SDL_Keycode key, unsigned scan, bool repeat) {
    a->last_input = SDL_GetTicks();
    bool back = key == SDLK_ESCAPE || scan == 482 || scan == 461;
    bool ok = key == SDLK_RETURN || key == SDLK_KP_ENTER || scan == 40;
    bool left = key == SDLK_LEFT || scan == 80, right = key == SDLK_RIGHT || scan == 79,
         up = key == SDLK_UP || scan == 82, down = key == SDLK_DOWN || scan == 81;
    bool play = key == SDLK_SPACE || scan == 450 || scan == 261 || scan == 415;
    if (back) {
        app_back(a);
        return;
    }
    if (scan == 505) {
        a->exit_confirm = true;
        return;
    }
    if (a->exit_confirm) {
        if (ok)
            a->quit = true;
        return;
    }
    if (a->keyboard) {
        if (key == SDLK_BACKSPACE) {
            erase_character(a->keyboard_field ? a->editing.api_key : a->editing.server);
            return;
        }
        if (key == SDLK_TAB) {
            a->keyboard = false;
            SDL_StopTextInput();
            return;
        }
        if (left)
            a->keyboard_index = grid_move(a->keyboard_index, -1, 0, 10, 60);
        if (right)
            a->keyboard_index = grid_move(a->keyboard_index, 1, 0, 10, 60);
        if (up)
            a->keyboard_index = grid_move(a->keyboard_index, 0, -1, 10, 60);
        if (down)
            a->keyboard_index = grid_move(a->keyboard_index, 0, 1, 10, 60);
        if (ok && !repeat) {
            int i = a->keyboard_index;
            const char *k = a->shift ? keys_upper : keys_lower;
            if (i < (int)strlen(k)) {
                char s[] = {k[i], 0};
                ui_text(a, s);
            } else if (i == 55)
                a->shift = !a->shift;
            else if (i == 56)
                erase_character(a->keyboard_field ? a->editing.api_key : a->editing.server);
            else if (i == 57)
                (a->keyboard_field ? a->editing.api_key : a->editing.server)[0] = 0;
            else if (i == 58) {
                a->keyboard = false;
                SDL_StopTextInput();
            } else if (i == 59)
                app_back(a);
        }
        return;
    }
    if (a->settings) {
        if (up)
            a->setting_focus = (a->setting_focus + 6) % 7;
        if (down)
            a->setting_focus = (a->setting_focus + 1) % 7;
        if (ok && !repeat)
            setting_activate(a);
        return;
    }
    if (a->viewer) {
        Asset *asset =
            a->catalog && a->selected < count(a) ? &a->catalog->assets[a->selected] : NULL;
        if (!asset)
            return;
        if (a->video) {
            if (ok || play) {
                a->paused = !a->paused;
                media_pause(a->paused);
            }
            if (left || right) {
                double target = fmax(0, media_position() + (right ? 10 : -10));
                if (asset->duration > 0)
                    target = fmin(target, fmax(0, asset->duration - .25));
                media_seek(target);
            }
            return;
        }
        if (left)
            viewer_change(a, -1);
        if (right)
            viewer_change(a, 1);
        if (up || key == SDLK_i)
            a->info = !a->info;
        if (down)
            a->zoom = a->zoom > 1 ? 1 : 1.6;
        if ((ok || play) && !repeat) {
            if (asset->video) {
                if (media_start(&a->config, asset, a->error)) {
                    a->video = true;
                    a->last_slide = SDL_GetTicks();
                }
                return;
            }
            a->slideshow = !a->slideshow;
            a->last_slide = SDL_GetTicks();
        }
        return;
    }
    if (key == SDLK_r && !repeat) {
        app_load(a, false);
        return;
    }
    if (key == SDLK_PAGEUP || scan == 300) {
        next_page(a, -1);
        return;
    }
    if (key == SDLK_PAGEDOWN || scan == 301) {
        next_page(a, 1);
        return;
    }
    if (a->sidebar) {
        if (up)
            a->nav = (a->nav + 4) % 5;
        if (down)
            a->nav = (a->nav + 1) % 5;
        if (right) {
            if (a->nav == 4) {
                a->editing = a->config;
                a->settings = true;
                a->setting_focus = 0;
            } else if (a->view != (View)a->nav || *a->album) {
                a->view = (View)a->nav;
                a->album[0] = 0;
                app_load(a, true);
            }
            a->sidebar = false;
        }
        if (ok && !repeat) {
            if (a->nav == 4) {
                a->editing = a->config;
                a->settings = true;
                a->setting_focus = 0;
            } else {
                a->view = (View)a->nav;
                a->album[0] = 0;
                app_load(a, true);
                a->sidebar = false;
            }
        }
        return;
    }
    if (a->loading || !count(a)) {
        if (left)
            a->sidebar = true;
        if (ok && !repeat)
            app_load(a, false);
        return;
    }
    if (left && a->selected % 5 == 0) {
        a->sidebar = true;
        return;
    }
    int selected = a->selected;
    if (left)
        selected = grid_move(selected, -1, 0, 5, count(a));
    if (right)
        selected = grid_move(selected, 1, 0, 5, count(a));
    if (up)
        selected = grid_move(selected, 0, -1, 5, count(a));
    if (down)
        selected = grid_move(selected, 0, 1, 5, count(a));
    if (down && selected == a->selected && a->selected / 5 == (count(a) - 1) / 5) {
        next_page(a, 1);
        return;
    }
    a->selected = selected;
    int row = selected / 5;
    if (row < a->scroll_row)
        a->scroll_row = row;
    if (row > a->scroll_row + 1)
        a->scroll_row = row - 1;
    if (ok && !repeat)
        app_open_selected(a);
    if (play && !repeat && !albums(a)) {
        app_open_selected(a);
        a->slideshow = true;
        a->last_slide = SDL_GetTicks();
    }
}

void ui_pointer(App *a, int x, int y, bool click) {
    if (a->keyboard) {
        if (x >= 430 && x < 1490 && y >= 444 && y < 900) {
            int col = (x - 430) / 106, row = (y - 444) / 76;
            if ((x - 430) % 106 >= 96 || (y - 444) % 76 >= 66)
                return;
            a->keyboard_index = row * 10 + col;
            if (click)
                ui_key(a, SDLK_RETURN, 0, false);
        }
        return;
    }
    if (a->viewer) {
        if (click)
            ui_key(a, SDLK_RETURN, 0, false);
        return;
    }
    if (a->settings) {
        if (x >= 600 && x <= 1500) {
            int f = -1;
            if (y >= 220 && y < 310)
                f = 0;
            if (y >= 330 && y < 420)
                f = 1;
            if (y >= 452 && y < 514)
                f = 2;
            if (y >= 524 && y < 586)
                f = 3;
            if (y >= 634 && y < 710)
                f = 4;
            if (y >= 758 && y < 814)
                f = 5;
            if (y >= 850 && y < 912)
                f = 6;
            if (f >= 0) {
                a->setting_focus = f;
                if (click)
                    setting_activate(a);
            }
        }
        return;
    }
    if (x >= 27 && x < 262 && y >= 234 && y < 626) {
        int nav = (y - 234) / 82;
        if (nav < 5 && (y - 234) % 82 < 64) {
            a->nav = nav;
            a->sidebar = true;
            if (click)
                ui_key(a, SDLK_RETURN, 0, false);
        }
        return;
    }
    if (x >= 350 && x < 1845 && y >= 600 && y < 1000 && !a->loading) {
        int col = (x - 350) / 300, row = (y - 600) / 206;
        int index = (a->scroll_row + row) * 5 + col;
        if (index < count(a)) {
            a->selected = index;
            a->sidebar = false;
            if (click)
                app_open_selected(a);
        }
    }
}

static void draw_sidebar(App *a) {
    fill(a, 0, 0, 290, 1080, (SDL_Color){13, 20, 32, 255});
    fill(a, 289, 0, 1, 1080, (SDL_Color){34, 44, 61, 255});
    logo(a, 74, 77, 25);
    text_box(a, "immich", (SDL_Rect){112, 48, 164, 56}, 5, WHITE, 0);
    text(a, "TV  /  时光放映室", 49, 119, 0, MUTED, 230);
    for (int i = 0; i < 5; i++) {
        int y = 244 + i * 82;
        bool active = (i == 4 ? a->settings : (a->view == (View)i && !a->settings));
        bool focused = a->sidebar && a->nav == i && !a->settings;
        if (active || focused)
            round_box(a, 27, y - 10, 235, 64, 12,
                      focused ? (SDL_Color){57, 78, 118, 255} : (SDL_Color){28, 42, 65, 255});
        if (active)
            fill(a, 28, y + 7, 3, 30, BLUE);
        nav_icon(a, i, 50, y + 6, active ? BLUE : MUTED);
        text_box(a, names[i], (SDL_Rect){102, y - 10, 145, 64}, 2, active ? WHITE : MUTED, 0);
    }
    circle(a, 51, 924, 5, a->connected ? (SDL_Color){114, 207, 164, 255} : MUTED);
    text_box(a, a->connected ? "已连接图库" : "尚未连接", (SDL_Rect){68, 906, 190, 36}, 0, MUTED,
             0);
    round_box(a, 39, 963, 127, 35, 9, (SDL_Color){24, 39, 44, 255});
    text_box(a, "只读浏览", (SDL_Rect){39, 963, 127, 35}, 0, (SDL_Color){151, 207, 183, 255}, 1);
    text_box(a, APP_VERSION, (SDL_Rect){203, 963, 60, 35}, 0, MUTED, 2);
}
static void draw_gallery(App *a, uint32_t now) {
    const char *title = *a->album ? a->album_name : names[a->view];
    text_box(a, title, (SDL_Rect){350, 52, 1100, 62}, 5, WHITE, 0);
    text_box(a, *a->album ? "相册中的每一帧，都有它的故事。" : subtitles[a->view],
             (SDL_Rect){352, 118, 1100, 36}, 1, MUTED, 0);
    time_t t = time(NULL);
    struct tm clock;
    localtime_r(&t, &clock);
    char timebuf[32];
    strftime(timebuf, sizeof timebuf, "%H:%M", &clock);
    text_box(a, timebuf, (SDL_Rect){1725, 52, 120, 62}, 2, MUTED, 2);
    SDL_Rect hero = {350, 185, 1495, 330};
    round_box(a, hero.x, hero.y, hero.w, hero.h, 20, (SDL_Color){29, 45, 58, 255});
    const char *hero_id = NULL;
    if (a->catalog && count(a) > 0)
        hero_id = albums(a) ? a->catalog->albums[0].cover_id : a->catalog->assets[0].id;
    if (hero_id)
        image_draw(a, get_image(a, hero_id, true), hero, false);
    SDL_RenderCopy(a->renderer, a->shade_horizontal, NULL, &hero);
    paint_clip_corners(a, hero, 20, BG);
    text_box(a, "YOUR LIFE, ON THE BIG SCREEN", (SDL_Rect){390, 225, 900, 32}, 0,
             (SDL_Color){174, 204, 241, 255}, 0);
    text_box(a, "把美好，留在眼前。", (SDL_Rect){386, 280, 1120, 80}, 6, WHITE, 0);
    text_box(a, "坐下来，慢慢重温。", (SDL_Rect){390, 370, 900, 44}, 2,
             (SDL_Color){213, 224, 239, 255}, 0);
    round_box(a, 391, 439, 260, 44, 22, (SDL_Color){230, 237, 251, 255});
    text_box(a, "选择照片，按 OK 查看", (SDL_Rect){391, 439, 260, 44}, 0,
             (SDL_Color){24, 39, 63, 255}, 1);
    char label[280];
    snprintf(label, sizeof label, "%s%s",
             *a->album ? "相册照片" : (albums(a) ? "你的相册" : "最近的时光"),
             a->page > 1 ? "  ·  继续浏览" : "");
    text_box(a, label, (SDL_Rect){351, 536, 1120, 48}, 2, WHITE, 0);
    snprintf(label, sizeof label, "第 %d 页  ·  %zu %s", a->page,
             a->catalog ? a->catalog->count : 0, albums(a) ? "个相册" : "个项目");
    text_box(a, label, (SDL_Rect){1550, 536, 295, 48}, 0, MUTED, 2);
    if (a->loading) {
        for (int i = 0; i < 10; i++) {
            int x = 350 + (i % 5) * 300, y = 600 + (i / 5) * 206;
            round_box(a, x, y, 275, 168, 10,
                      (SDL_Color){22 + (Uint8)((now / 50 + i) % 6), 31, 46, 255});
        }
        text(a, "正在整理你的时光…", 352, 1015, 0, MUTED, 1000);
        return;
    }
    if (*a->error) {
        text(a, a->error, 375, 655, 3, WHITE, 1400);
        text(a, "按 OK 重试，或按返回键进入侧栏调整连接。", 375, 724, 1, MUTED, 1300);
        return;
    }
    if (!count(a)) {
        text_box(a, "这里还没有内容", (SDL_Rect){350, 640, 1495, 80}, 4, WHITE, 1);
        text_box(a, "换个分类看看，或在 Immich 中检查照片和 API 密钥权限。",
                 (SDL_Rect){350, 730, 1495, 55}, 1, MUTED, 1);
        return;
    }
    for (int i = 0; i < 10; i++) {
        int index = a->scroll_row * 5 + i;
        if (index >= count(a))
            break;
        int x = 350 + (i % 5) * 300, y = 600 + (i / 5) * 206;
        SDL_Rect box = {x, y, 275, 166};
        round_box(a, x, y, 275, 166, 12, (SDL_Color){26, 36, 52, 255});
        const char *id =
            albums(a) ? a->catalog->albums[index].cover_id : a->catalog->assets[index].id;
        ImageSlot *image = get_image(a, id, false);
        image_draw(a, image, box, false);
        paint_clip_corners(a, box, 12, BG);
        if (!valid_asset_id(id) && albums(a))
            paint_icon(a, 1, (SDL_Rect){x + 110, y + 55, 55, 55}, (SDL_Color){100, 117, 142, 255});
        if (image && image->failed)
            text_box(a, "缩略图不可用", box, 0, MUTED, 1);
        if (!a->sidebar && index == a->selected) {
            if (!a->focus_valid) {
                a->focus_x = x;
                a->focus_y = y;
                a->focus_valid = true;
            }
            a->focus_x += (x - a->focus_x) * .35f;
            a->focus_y += (y - a->focus_y) * .35f;
            outline(a, (SDL_Rect){(int)a->focus_x - 7, (int)a->focus_y - 7, 289, 180},
                    (SDL_Color){170, 201, 255, 255}, 3);
        }
        if (albums(a)) {
            Album *b = &a->catalog->albums[index];
            text_box(a, b->name, (SDL_Rect){x, y + 174, 195, 34}, 1, WHITE, 0);
            snprintf(label, sizeof label, "%d 张", b->count);
            text_box(a, label, (SDL_Rect){x + 200, y + 174, 75, 34}, 0, MUTED, 2);
        } else {
            Asset *b = &a->catalog->assets[index];
            if (b->video) {
                snprintf(label, sizeof label, "%d:%02d", (int)b->duration / 60,
                         (int)b->duration % 60);
                TextSlot *duration = text_slot(a, label, 0, WHITE);
                int label_width = duration ? duration->ink.w : 42;
                int badge_width = label_width + 38;
                round_box(a, x + 12, y + 120, badge_width, 34, 9, (SDL_Color){0, 0, 0, 180});
                paint_icon(a, 3, (SDL_Rect){x + 21, y + 130, 14, 14}, WHITE);
                text_box(a, label, (SDL_Rect){x + 40, y + 120, label_width, 34}, 0, WHITE, 1);
            }
            if (b->favorite) {
                round_box(a, x + 230, y + 8, 34, 34, 17, (SDL_Color){4, 10, 18, 135});
                paint_icon(a, 5, (SDL_Rect){x + 237, y + 15, 20, 20}, WHITE);
            }
            if (*b->city)
                copy_text(label, sizeof label, b->city);
            else
                snprintf(label, sizeof label, "%.10s", b->date);
            text_box(a, label, (SDL_Rect){x, y + 174, 275, 34}, 0,
                     index == a->selected && !a->sidebar ? WHITE : MUTED, 0);
        }
    }
    text(a, "← → ↑ ↓  选择       OK  查看       返回  侧栏       CH ±  翻页", 352, 1028, 0, MUTED,
         1300);
}
static void draw_viewer(App *a, uint32_t now) {
    Asset *b = &a->catalog->assets[a->selected];
    if (a->video) {
        SDL_SetRenderDrawColor(a->renderer, 0, 0, 0, 0);
        SDL_RenderClear(a->renderer);
        FrameRect frame = fit_frame(b->width, b->height, 1920, 1080);
        SDL_Color black = {0, 0, 0, 255};
        if (frame.x > 0) {
            fill(a, 0, 0, frame.x, 1080, black);
            fill(a, frame.x + frame.w, 0, 1920 - frame.x - frame.w, 1080, black);
        }
        if (frame.y > 0) {
            fill(a, 0, 0, 1920, frame.y, black);
            fill(a, 0, frame.y + frame.h, 1920, 1080 - frame.y - frame.h, black);
        }
        int state = media_state();
        if (state < 0) {
            text(a, "视频暂时无法播放", 90, 790, 5, WHITE, 1700);
            text(a, media_error(), 90, 872, 2, MUTED, 1700);
        } else if (state == 0)
            text(a, "正在准备视频…", 90, 872, 3, WHITE, 1700);
        if (now - a->last_input < 5000 || a->paused || state < 0) {
            fill(a, 0, 926, 1920, 154, (SDL_Color){4, 10, 18, 220});
            text(a, b->name, 78, 944, 2, WHITE, 1200);
            text(a, a->paused ? "已暂停 · OK 继续" : "OK 暂停    ← / → 跳转 10 秒    返回 停止播放",
                 78, 1002, 1, MUTED, 1700);
        }
        return;
    }
    fill(a, 0, 0, 1920, 1080, (SDL_Color){3, 6, 10, 255});
    ImageSlot *slot = get_image(a, b->id, true);
    if (slot && slot->texture) {
        int w = (int)(1920 * a->zoom), h = (int)(1080 * a->zoom);
        image_draw(a, slot, (SDL_Rect){(1920 - w) / 2, (1080 - h) / 2, w, h}, true);
    } else
        text_box(a, slot && slot->failed ? "这张照片暂时无法显示" : "正在载入清晰照片…",
                 (SDL_Rect){0, 0, 1920, 1080}, 2, MUTED, 1);
    if (a->info || now - a->last_input < 4500 || a->slideshow || b->video) {
        SDL_Rect shade_rect = {0, 800, 1920, 280};
        SDL_RenderCopy(a->renderer, a->shade_vertical, NULL, &shade_rect);
        char label[480];
        snprintf(label, sizeof label, "%d / %d    %.10s%s%s", a->selected + 1, count(a), b->date,
                 *b->city ? "  ·  " : "", b->city);
        text(a, label, 74, 885, 1, (SDL_Color){215, 225, 239, 255}, 1700);
        text(a, b->name, 71, 930, 3, WHITE, 1600);
        const char *hint =
            b->video ? "OK  播放视频     ← →  切换     ↑  信息     返回  图库"
            : a->slideshow
                ? "幻灯片播放中    OK 暂停     ← → 切换     返回 图库"
                : "← →  切换     OK  幻灯片     ↑  信息     ↓  放大 / 还原     返回  图库";
        text(a, hint, 74, 1017, 0, (SDL_Color){179, 195, 217, 255}, 1750);
    }
    if (a->info) {
        round_box(a, 1330, 75, 510, 270, 16, (SDL_Color){15, 24, 39, 235});
        text(a, "照片信息", 1360, 96, 3, WHITE, 450);
        char s[160];
        snprintf(s, sizeof s, "%d × %d%s", b->width, b->height, b->video ? "  ·  视频" : "");
        text(a, s, 1360, 164, 1, MUTED, 450);
        text(a, b->camera, 1360, 213, 1, MUTED, 450);
        text(a, b->favorite ? "已在 Immich 收藏" : "", 1360, 262, 1, BLUE, 450);
    }
}
static void draw_settings(App *a) {
    text(a, "连接你的 Immich", 600, 70, 5, WHITE, 1150);
    text(a, "照片留在你的服务器，回忆在电视上展开。", 603, 140, 1, MUTED, 1150);
    const char *labels[] = {"服务器地址", "只读 API 密钥"};
    for (int i = 0; i < 2; i++) {
        int y = 220 + i * 110;
        bool focus = a->setting_focus == i;
        round_box(a, 600, y, 900, 90, 12,
                  focus ? (SDL_Color){31, 48, 77, 255} : (SDL_Color){21, 31, 47, 255});
        if (focus)
            outline(a, (SDL_Rect){600, y, 900, 90}, BLUE, 2);
        text_box(a, labels[i], (SDL_Rect){620, y + 8, 850, 26}, 0, MUTED, 0);
        char masked[160];
        const char *s = i ? a->editing.api_key : a->editing.server;
        if (i && *s) {
            size_t n = strlen(s);
            if (n > 56)
                n = 56;
            memset(masked, '*', n);
            masked[n] = 0;
            s = masked;
        }
        text_box(a,
                 *s ? s : (i ? "在 Immich 账户设置中创建只读密钥" : "https://photos.example.com"),
                 (SDL_Rect){620, y + 39, 855, 42}, 2, *s ? WHITE : MUTED, 0);
    }
    const char *toggles[] = {"允许 HTTP 连接（仅用于可信的局域网）", "在本机记住连接"};
    bool enabled[] = {a->editing.allow_http, a->editing.remember};
    for (int i = 0; i < 2; i++) {
        int y = 452 + i * 72;
        if (a->setting_focus == i + 2)
            round_box(a, 594, y, 912, 62, 12, (SDL_Color){30, 44, 66, 255});
        text_box(a, toggles[i], (SDL_Rect){620, y, 770, 62}, 1, WHITE, 0);
        round_box(a, 1415, y + 15, 60, 32, 16,
                  enabled[i] ? (SDL_Color){104, 156, 238, 255} : (SDL_Color){56, 67, 84, 255});
        circle(a, enabled[i] ? 1459 : 1431, y + 31, 12, WHITE);
    }
    round_box(a, 600, 634, 900, 76, 12,
              a->setting_focus == 4 ? (SDL_Color){179, 207, 255, 255}
                                    : (SDL_Color){105, 156, 237, 255});
    text_box(a, "连接图库", (SDL_Rect){600, 634, 900, 76}, 3, (SDL_Color){12, 24, 45, 255}, 1);
    text_box(a, "只读访问你的照片与相册", (SDL_Rect){600, 723, 900, 32}, 0, MUTED, 1);
    if (a->setting_focus == 5)
        round_box(a, 594, 758, 920, 56, 10, (SDL_Color){45, 31, 40, 255});
    text_box(a, "清除本机连接", (SDL_Rect){620, 758, 860, 56}, 1, (SDL_Color){226, 158, 163, 255},
             0);
    if (a->setting_focus == 6)
        round_box(a, 594, 850, 920, 62, 10, (SDL_Color){30, 44, 66, 255});
    char s[160];
    snprintf(s, sizeof s, "幻灯片间隔     %d 秒", a->editing.slideshow_seconds);
    text_box(a, s, (SDL_Rect){620, 850, 860, 62}, 1, WHITE, 0);
    if (*a->error)
        text(a, a->error, 600, 947, 1, (SDL_Color){244, 160, 160, 255}, 1150);
    else if (*a->notice)
        text(a, a->notice, 600, 947, 1, BLUE, 1150);
    text(a, "↑ ↓  选择     OK  编辑 / 切换     返回  取消", 600, 1024, 0, MUTED, 1100);
}
static void draw_keyboard(App *a) {
    fill(a, 0, 0, 1920, 1080, (SDL_Color){0, 0, 0, 195});
    round_box(a, 390, 220, 1140, 720, 18, (SDL_Color){18, 28, 45, 255});
    text(a, a->keyboard_field ? "输入 API 密钥" : "输入服务器地址", 430, 250, 4, WHITE, 1060);
    const char *value = a->keyboard_field ? a->editing.api_key : a->editing.server;
    char masked[560];
    if (a->keyboard_field) {
        size_t n = strlen(value);
        memset(masked, '*', n);
        masked[n] = 0;
        value = masked;
    }
    round_box(a, 430, 331, 1060, 79, 9, (SDL_Color){9, 17, 30, 255});
    text_box(a, value, (SDL_Rect){452, 331, 1010, 79}, 2, WHITE, 0);
    const char *k = a->shift ? keys_upper : keys_lower;
    for (int i = 0; i < 60; i++) {
        int x = 430 + i % 10 * 106, y = 444 + i / 10 * 76;
        round_box(a, x, y, 96, 66, 8,
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
        text_box(a, s, (SDL_Rect){x, y, 96, 66}, 2, i == a->keyboard_index ? BG : WHITE, 1);
    }
    text(a, "支持遥控器、魔法指针或实体键盘。返回键取消本次编辑。", 430, 908, 0, MUTED, 1060);
}
void ui_draw(App *a, uint32_t now) {
    ++a->frames;
    SDL_SetRenderDrawColor(a->renderer, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(a->renderer);
    if (a->viewer && a->catalog && !albums(a) && count(a) > 0)
        draw_viewer(a, now);
    else {
        draw_sidebar(a);
        if (a->settings)
            draw_settings(a);
        else
            draw_gallery(a, now);
    }
    if (a->keyboard)
        draw_keyboard(a);
    if (*a->error && a->viewer && !a->video) {
        round_box(a, 300, 70, 1320, 75, 12, (SDL_Color){67, 34, 43, 240});
        text_box(a, a->error, (SDL_Rect){330, 70, 1260, 75}, 1, WHITE, 1);
    }
    if (a->exit_confirm) {
        fill(a, 0, 0, 1920, 1080, (SDL_Color){0, 0, 0, 185});
        round_box(a, 580, 360, 760, 340, 20, (SDL_Color){20, 31, 48, 255});
        text_box(a, "离开放映室？", (SDL_Rect){610, 400, 700, 90}, 5, WHITE, 1);
        text_box(a, "按 OK 退出，按返回键继续浏览。", (SDL_Rect){610, 516, 700, 65}, 2, MUTED, 1);
    }
}
void app_tick(App *a, uint32_t now) {
    app_results(a);
    if (a->viewer && a->slideshow && !a->video &&
        now - a->last_slide > (uint32_t)a->config.slideshow_seconds * 1000) {
        int next = a->selected + 1;
        while (next < count(a) && a->catalog->assets[next].video)
            ++next;
        if (next >= count(a)) {
            a->slideshow = false;
            return;
        }
        a->selected = next;
        a->last_slide = now;
        a->zoom = 1;
        get_image(a, a->catalog->assets[next].id, true);
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
bool ui_capture(App *a, const char *path) {
    int w, h;
    SDL_GetRendererOutputSize(a->renderer, &w, &h);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!s)
        return false;
    bool ok =
        SDL_RenderReadPixels(a->renderer, NULL, s->format->format, s->pixels, s->pitch) == 0 &&
        IMG_SavePNG(s, path) == 0;
    SDL_FreeSurface(s);
#ifdef ENABLE_QA
    if (ok) {
        char metadata[1400];
        snprintf(metadata, sizeof metadata, "%s.json", path);
        FILE *file = fopen(metadata, "w");
        if (file) {
            fprintf(file,
                    "{\"view\":%d,\"page\":%d,\"count\":%zu,\"selected\":%d,\"viewer\":%s,"
                    "\"video\":%s,\"paused\":%s,\"slideshow\":%s,\"loading\":%s}\n",
                    a->view, a->page, a->catalog ? a->catalog->count : 0, a->selected,
                    a->viewer ? "true" : "false", a->video ? "true" : "false",
                    a->paused ? "true" : "false", a->slideshow ? "true" : "false",
                    a->loading ? "true" : "false");
            fclose(file);
        }
    }
#endif
    return ok;
}
