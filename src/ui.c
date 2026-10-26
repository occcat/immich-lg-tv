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
static const char *subtitles[] = {"按拍摄时间，重温生活里的每一刻。",
                                  "把一段旅程、一个周末，慢慢看完。", "那些想再看一次的瞬间。",
                                  "让回忆，重新动起来。"};
static const char *keys_lower = "1234567890qwertyuiopasdfghjkl:zxcvbnm./-@_?=&%[]+#!$~*,";
static const char *keys_upper = "1234567890QWERTYUIOPASDFGHJKL:ZXCVBNM./-@_?=&%[]+#!$~*,";

/* Rendering, remote navigation and pointer hit tests share these dimensions. */
typedef struct {
    int columns, rows, width, height, step_x, step_y;
} GridLayout;
static GridLayout gallery_layout(const App *a) {
    bool large = (a->view == VIEW_ALBUMS && !*a->album) || a->view == VIEW_VIDEOS;
    return large ? (GridLayout){3, 2, 496, 248, 520, 336}
                 : (GridLayout){5, 3, 288, 162, 312, 224};
}
static SDL_Rect gallery_card(GridLayout g, int visible_index) {
    return (SDL_Rect){304 + visible_index % g.columns * g.step_x,
                      312 + visible_index / g.columns * g.step_y, g.width, g.height};
}
static SDL_Rect nav_rect(int index) {
    return (SDL_Rect){24, index == 4 ? 808 : 248 + index * 90, 192, 68};
}
static SDL_Rect setting_rect(int index) {
    const SDL_Rect rects[] = {{336, 300, 936, 100}, {336, 424, 936, 100},
                             {336, 552, 936, 72},  {336, 640, 936, 72},
                             {336, 760, 936, 76},  {336, 878, 936, 60},
                             {1360, 760, 480, 108}};
    return rects[index];
}
static bool inside(SDL_Rect r, int x, int y) {
    SDL_Point point = {x, y};
    return SDL_PointInRect(&point, &r);
}

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
    if (t->ink.w > box.w) {
        /* Cache elision by label and width, preserving UTF-8 code points. */
        if (t->fitted_width != box.w) {
            int boundaries[640], count = 1;
            boundaries[0] = 0;
            for (int i = 1; i < 635 && s[i - 1]; ++i)
                if (((unsigned char)s[i] & 0xc0) != 0x80)
                    boundaries[count++] = i;
            int low = 0, high = count - 1, best = -1;
            while (low <= high) {
                int mid = (low + high) / 2, w, h;
                snprintf(t->fitted, sizeof t->fitted, "%.*s…", boundaries[mid], s);
                if (TTF_SizeUTF8(a->fonts[size], t->fitted, &w, &h) == 0 && w <= box.w) {
                    best = mid;
                    low = mid + 1;
                } else
                    high = mid - 1;
            }
            if (best < 0)
                t->fitted[0] = 0;
            else
                snprintf(t->fitted, sizeof t->fitted, "%.*s…", boundaries[best], s);
            t->fitted_width = box.w;
        }
        char fitted[640];
        copy_text(fitted, sizeof fitted, t->fitted);
        t = text_slot(a, fitted, size, c);
        if (!t || !t->texture)
            return;
    }
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
    SDL_Surface *shade = SDL_CreateRGBSurfaceWithFormat(0, 1, 280, 32, SDL_PIXELFORMAT_RGBA32);
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
        GridLayout g = gallery_layout(a);
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
    GridLayout g = gallery_layout(a);
    if (left && a->selected % g.columns == 0) {
        a->sidebar = true;
        return;
    }
    int selected = a->selected;
    if (left)
        selected = grid_move(selected, -1, 0, g.columns, count(a));
    if (right)
        selected = grid_move(selected, 1, 0, g.columns, count(a));
    if (up)
        selected = grid_move(selected, 0, -1, g.columns, count(a));
    if (down)
        selected = grid_move(selected, 0, 1, g.columns, count(a));
    if (down && selected == a->selected && a->selected / g.columns == (count(a) - 1) / g.columns) {
        next_page(a, 1);
        return;
    }
    a->selected = selected;
    int row = selected / g.columns;
    if (row < a->scroll_row)
        a->scroll_row = row;
    if (row >= a->scroll_row + g.rows)
        a->scroll_row = row - g.rows + 1;
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
        for (int i = 0; i < 7; ++i)
            if (inside(setting_rect(i), x, y)) {
                a->setting_focus = i;
                if (click)
                    setting_activate(a);
                return;
            }
    }
    for (int i = 0; i < 5; ++i)
        if (inside(nav_rect(i), x, y)) {
            if (a->settings && !click)
                return;
            if (a->settings) {
                a->settings = false;
                a->editing = a->config;
            }
            a->nav = i;
            a->sidebar = true;
            if (click)
                ui_key(a, SDLK_RETURN, 0, false);
            return;
        }
    if (a->settings || a->loading)
        return;
    GridLayout g = gallery_layout(a);
    for (int i = 0; i < g.columns * g.rows; ++i) {
        SDL_Rect hit = gallery_card(g, i);
        hit.h += 48;
        int index = a->scroll_row * g.columns + i;
        if (index < count(a) && inside(hit, x, y)) {
            a->selected = index;
            a->sidebar = false;
            if (click)
                app_open_selected(a);
            return;
        }
    }
}

static void draw_sidebar(App *a) {
    fill(a, 0, 0, 240, 1080, (SDL_Color){14, 21, 33, 255});
    logo(a, 55, 82, 22);
    text_box(a, "immich", (SDL_Rect){88, 54, 128, 56}, 4, WHITE, 0);
    text_box(a, "电视图库", (SDL_Rect){38, 120, 178, 32}, 0, MUTED, 0);
    text_box(a, "浏览", (SDL_Rect){40, 202, 140, 28}, 0, MUTED, 0);
    for (int i = 0; i < 5; i++) {
        SDL_Rect r = nav_rect(i);
        bool active = i == 4 ? a->settings : (a->view == (View)i && !a->settings);
        bool focused = a->sidebar && a->nav == i && !a->settings;
        if (active || focused)
            paint_round(a, r, 16, focused ? (SDL_Color){49, 69, 105, 255}
                                         : (SDL_Color){27, 40, 61, 255});
        if (focused)
            paint_ring(a, r, 16, 2, (SDL_Color){145, 180, 237, 255});
        nav_icon(a, i, r.x + 18, r.y + 18, active || focused ? BLUE : MUTED);
        text_box(a, names[i], (SDL_Rect){r.x + 68, r.y, 116, r.h}, 2,
                 active || focused ? WHITE : MUTED, 0);
    }
    fill(a, 40, 776, 160, 1, (SDL_Color){34, 44, 61, 255});
    circle(a, 42, 943, 4, a->connected ? (SDL_Color){114, 207, 164, 255} : MUTED);
    text_box(a, a->connected ? "图库已连接" : "尚未连接", (SDL_Rect){56, 928, 156, 30}, 0, MUTED, 0);
    text_box(a, "只读浏览", (SDL_Rect){39, 973, 124, 32}, 0,
             (SDL_Color){151, 207, 183, 255}, 0);
    text_box(a, APP_VERSION, (SDL_Rect){160, 973, 48, 32}, 0, MUTED, 2);
}
static void draw_header(App *a, const char *title, const char *subtitle) {
    text_box(a, *a->album && !a->settings ? "相册 / 照片" : "我的图库",
             (SDL_Rect){304, 52, 1100, 30}, 0, BLUE, 0);
    text_box(a, title, (SDL_Rect){300, 98, 1280, 76}, 6, WHITE, 0);
    text_box(a, subtitle, (SDL_Rect){304, 188, 1280, 32}, 1, MUTED, 0);
    time_t t = time(NULL);
    struct tm clock;
    localtime_r(&t, &clock);
    char label[64];
    strftime(label, sizeof label, "%H:%M", &clock);
    text_box(a, label, (SDL_Rect){1650, 60, 190, 62}, 5, WHITE, 2);
    snprintf(label, sizeof label, "%d 月 %d 日", clock.tm_mon + 1, clock.tm_mday);
    text_box(a, label, (SDL_Rect){1620, 132, 220, 36}, 1, MUTED, 2);
}
static int key_hint(App *a, int x, const char *key, const char *label, int key_width, int width) {
    round_box(a, x, 1020, key_width, 34, 8, (SDL_Color){29, 40, 57, 255});
    text_box(a, key, (SDL_Rect){x, 1020, key_width, 34}, 0, WHITE, 1);
    text_box(a, label, (SDL_Rect){x + key_width + 12, 1020, width, 34}, 0, MUTED, 0);
    return x + key_width + width + 44;
}
static void draw_footer(App *a, bool settings) {
    fill(a, 304, 995, 1536, 1, (SDL_Color){30, 40, 56, 255});
    int x = key_hint(a, 304, "方向键", settings ? "选择" : "浏览", 84, 72);
    x = key_hint(a, x, "OK", settings ? "编辑 / 切换" : "打开", 50, settings ? 130 : 72);
    key_hint(a, x, "返回", settings ? "回到图库" : "导航", 64, 112);
    if (!settings && !albums(a))
        key_hint(a, 1608, "CH ±", "翻页", 72, 72);
}
static void draw_gallery(App *a, uint32_t now) {
    draw_header(a, *a->album ? a->album_name : names[a->view],
                *a->album ? "一起回看，相册里的每一段故事。" : subtitles[a->view]);
    GridLayout g = gallery_layout(a);
    const char *section = *a->album ? "相册中的照片" : albums(a) ? "全部相册"
                          : a->view == VIEW_FAVORITES ? "我的收藏"
                          : a->view == VIEW_VIDEOS ? "全部视频" : "最近拍摄";
    text_box(a, section, (SDL_Rect){304, 252, 1050, 40}, 2, WHITE, 0);
    char label[320];
    if (albums(a))
        snprintf(label, sizeof label, "%zu 个相册", a->catalog ? a->catalog->count : 0);
    else
        snprintf(label, sizeof label, "第 %d 页  /  %zu 项", a->page,
                 a->catalog ? a->catalog->count : 0);
    text_box(a, label, (SDL_Rect){1450, 252, 390, 40}, 1, MUTED, 2);
    draw_footer(a, false);
    if (a->loading) {
        for (int i = 0; i < g.columns * g.rows; i++) {
            SDL_Rect r = gallery_card(g, i);
            paint_round(a, r, 14, (SDL_Color){22 + (Uint8)((now / 70 + i) % 4), 31, 46, 255});
            round_box(a, r.x, r.y + r.h + 14, 124, 18, 6, (SDL_Color){23, 32, 46, 255});
        }
        return;
    }
    if (*a->error || !count(a)) {
        paint_icon(a, *a->error ? 4 : albums(a) ? 1 : 0, (SDL_Rect){1028, 484, 88, 88}, MUTED);
        text_box(a, *a->error ? "暂时无法读取图库" : "这里还没有内容",
                 (SDL_Rect){464, 616, 1216, 70}, 4, WHITE, 1);
        text_box(a, *a->error ? a->error : "换个分类看看，或在 Immich 中添加照片。",
                 (SDL_Rect){464, 704, 1216, 48}, 1, MUTED, 1);
        if (*a->error)
            text_box(a, "按 OK 重试，按返回键检查连接。", (SDL_Rect){464, 774, 1216, 48}, 1, BLUE, 1);
        return;
    }
    for (int i = 0; i < g.columns * g.rows; i++) {
        int index = a->scroll_row * g.columns + i;
        if (index >= count(a))
            break;
        SDL_Rect box = gallery_card(g, i);
        int x = box.x, y = box.y;
        paint_round(a, box, 14, (SDL_Color){26, 36, 52, 255});
        const char *id = albums(a) ? a->catalog->albums[index].cover_id
                                  : a->catalog->assets[index].id;
        ImageSlot *image = get_image(a, id, false);
        image_draw(a, image, box, false);
        paint_clip_corners(a, box, 14, BG);
        if (!valid_asset_id(id) && albums(a))
            paint_icon(a, 1, (SDL_Rect){x + (box.w - 72) / 2, y + (box.h - 72) / 2, 72, 72},
                       (SDL_Color){100, 117, 142, 255});
        if (image && image->failed)
            text_box(a, "缩略图不可用", box, 0, MUTED, 1);
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
        SDL_Color caption = selected ? WHITE : (SDL_Color){177, 190, 210, 255};
        if (albums(a)) {
            Album *b = &a->catalog->albums[index];
            text_box(a, b->name, (SDL_Rect){x, y + box.h + 12, box.w - 114, 44}, 2, caption, 0);
            snprintf(label, sizeof label, "%d 张", b->count);
            text_box(a, label, (SDL_Rect){x + box.w - 100, y + box.h + 12, 100, 44}, 1, MUTED, 2);
        } else {
            Asset *b = &a->catalog->assets[index];
            if (b->video) {
                snprintf(label, sizeof label, "%d:%02d", (int)b->duration / 60, (int)b->duration % 60);
                TextSlot *duration = text_slot(a, label, 0, WHITE);
                int label_width = duration ? duration->ink.w : 42;
                round_box(a, x + 12, y + box.h - 46, label_width + 38, 34, 9,
                          (SDL_Color){0, 0, 0, 180});
                paint_icon(a, 3, (SDL_Rect){x + 21, y + box.h - 36, 14, 14}, WHITE);
                text_box(a, label, (SDL_Rect){x + 40, y + box.h - 46, label_width, 34}, 0, WHITE, 1);
            }
            if (b->favorite) {
                round_box(a, x + box.w - 46, y + 12, 34, 34, 17, (SDL_Color){4, 10, 18, 155});
                paint_icon(a, 5, (SDL_Rect){x + box.w - 39, y + 19, 20, 20}, WHITE);
            }
            if (a->view == VIEW_VIDEOS)
                copy_text(label, sizeof label, b->name);
            else if (*b->city)
                copy_text(label, sizeof label, b->city);
            else
                snprintf(label, sizeof label, "%.10s", b->date);
            text_box(a, label, (SDL_Rect){x, y + box.h + 12, box.w - 88, 40},
                     a->view == VIEW_VIDEOS ? 2 : 1, caption, 0);
            if (strlen(b->date) >= 10)
                snprintf(label, sizeof label, "%.2s.%.2s", b->date + 5, b->date + 8);
            else
                label[0] = 0;
            text_box(a, label, (SDL_Rect){x + box.w - 80, y + box.h + 12, 80, 40}, 0, MUTED, 2);
        }
    }
    /* Visible range makes scroll position clear without inventing a total count
       beyond the current API page. */
    if (count(a) > g.columns * g.rows) {
        int visible = g.columns * g.rows;
        int last = a->scroll_row * g.columns + visible;
        if (last > count(a))
            last = count(a);
        snprintf(label, sizeof label, "%d – %d / %d", a->scroll_row * g.columns + 1, last, count(a));
        text_box(a, label, (SDL_Rect){1300, 1020, 260, 34}, 0, MUTED, 2);
    }
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
    draw_header(a, "连接设置", "连接你的 Immich，在大屏上回看自己的照片。");
    round_box(a, 304, 268, 1000, 690, 20, (SDL_Color){15, 23, 37, 255});
    const char *labels[] = {"服务器地址", "只读 API 密钥"};
    for (int i = 0; i < 2; i++) {
        SDL_Rect r = setting_rect(i);
        bool focus = a->setting_focus == i;
        paint_round(a, r, 12, focus ? (SDL_Color){31, 48, 77, 255} : (SDL_Color){23, 34, 51, 255});
        if (focus)
            outline(a, r, BLUE, 2);
        text_box(a, labels[i], (SDL_Rect){r.x + 20, r.y + 12, r.w - 40, 26}, 0, MUTED, 0);
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
        text_box(a, *value ? value : i ? "输入你的只读 API 密钥" : "https://photos.example.com",
                 (SDL_Rect){r.x + 20, r.y + 46, r.w - 40, 40}, 2, *value ? WHITE : MUTED, 0);
    }
    const char *titles[] = {"允许 HTTP 连接", "在本机记住连接"};
    const char *details[] = {"仅在可信的局域网中开启", "下次打开时，自动进入你的图库"};
    bool enabled[] = {a->editing.allow_http, a->editing.remember};
    for (int i = 0; i < 2; i++) {
        SDL_Rect r = setting_rect(i + 2);
        if (a->setting_focus == i + 2)
            paint_round(a, r, 12, (SDL_Color){30, 44, 66, 255});
        text_box(a, titles[i], (SDL_Rect){r.x + 20, r.y + 4, 730, 32}, 1, WHITE, 0);
        text_box(a, details[i], (SDL_Rect){r.x + 20, r.y + 42, 730, 26}, 0, MUTED, 0);
        round_box(a, r.x + r.w - 80, r.y + 20, 60, 32, 16,
                  enabled[i] ? (SDL_Color){104, 156, 238, 255} : (SDL_Color){56, 67, 84, 255});
        circle(a, r.x + r.w - (enabled[i] ? 36 : 64), r.y + 36, 12, WHITE);
    }
    SDL_Rect connect = setting_rect(4);
    paint_round(a, connect, 12, a->setting_focus == 4 ? (SDL_Color){179, 207, 255, 255}
                                                  : (SDL_Color){105, 156, 237, 255});
    text_box(a, "连接图库", connect, 3, (SDL_Color){12, 24, 45, 255}, 1);
    text_box(a, "只读访问照片、相册与视频", (SDL_Rect){336, 844, 936, 26}, 0, MUTED, 1);
    SDL_Rect forget = setting_rect(5);
    if (a->setting_focus == 5)
        paint_round(a, forget, 12, (SDL_Color){45, 31, 40, 255});
    text_box(a, "清除本机连接", (SDL_Rect){356, 878, 260, 60}, 1,
             (SDL_Color){226, 158, 163, 255}, 0);
    text_box(a, "仅移除此电视上的连接信息", (SDL_Rect){716, 878, 536, 60}, 0, MUTED, 2);
    round_box(a, 1360, 268, 480, 444, 20, (SDL_Color){20, 30, 46, 255});
    logo(a, 1416, 324, 24);
    text_box(a, "让回忆，回到眼前。", (SDL_Rect){1392, 376, 416, 48}, 3, WHITE, 0);
    text_box(a, "在 Immich 账户设置中创建", (SDL_Rect){1392, 458, 416, 36}, 1, MUTED, 0);
    text_box(a, "只读 API 密钥，填入左侧。", (SDL_Rect){1392, 502, 416, 36}, 1, MUTED, 0);
    fill(a, 1392, 574, 416, 1, (SDL_Color){42, 54, 73, 255});
    text_box(a, "照片留在你的服务器。", (SDL_Rect){1392, 610, 416, 36}, 1,
             (SDL_Color){180, 197, 219, 255}, 0);
    SDL_Rect interval = setting_rect(6);
    paint_round(a, interval, 16, (SDL_Color){20, 30, 46, 255});
    if (a->setting_focus == 6)
        paint_ring(a, interval, 16, 2, BLUE);
    text_box(a, "幻灯片间隔", (SDL_Rect){1392, 778, 250, 38}, 1, WHITE, 0);
    char value[96];
    snprintf(value, sizeof value, "%d 秒", a->editing.slideshow_seconds);
    text_box(a, value, (SDL_Rect){1680, 776, 128, 42}, 3, WHITE, 2);
    text_box(a, "按 OK 切换播放间隔", (SDL_Rect){1392, 824, 416, 28}, 0, MUTED, 0);
    if (*a->error || *a->notice)
        text_box(a, *a->error ? a->error : a->notice, (SDL_Rect){1360, 890, 480, 70}, 0,
                 *a->error ? (SDL_Color){244, 160, 160, 255} : BLUE, 0);
    draw_footer(a, true);
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
            GridLayout g = gallery_layout(a);
            fprintf(file,
                    "{\"view\":%d,\"page\":%d,\"count\":%zu,\"selected\":%d,\"viewer\":%s,"
                    "\"video\":%s,\"paused\":%s,\"slideshow\":%s,\"loading\":%s,"
                    "\"columns\":%d,\"rows\":%d,\"scrollRow\":%d,\"sidebar\":%s,"
                    "\"settings\":%s,\"keyboard\":%s}\n",
                    a->view, a->page, a->catalog ? a->catalog->count : 0, a->selected,
                    a->viewer ? "true" : "false", a->video ? "true" : "false",
                    a->paused ? "true" : "false", a->slideshow ? "true" : "false",
                    a->loading ? "true" : "false", g.columns, g.rows, a->scroll_row,
                    a->sidebar ? "true" : "false", a->settings ? "true" : "false",
                    a->keyboard ? "true" : "false");
            fclose(file);
        }
    }
#endif
    return ok;
}
