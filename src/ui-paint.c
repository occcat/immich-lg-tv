#include "ui-paint.h"
#include "vector.h"
#include <math.h>
#include <stdio.h>

static float coverage(float distance) {
    return fminf(1, fmaxf(0, .5f - distance));
}
/* One textured quad per shape avoids seams between separately scaled corners.
 * Geometry is cached independently of color. Its signed-distance coverage keeps
 * a one-pixel antialiasing fringe at every radius, including narrow pills. */
static SDL_Texture *mask(App *a, int w, int h, int radius, int kind) {
    if (w <= 0 || h <= 0)
        return NULL;
    ShapeSlot *slot = NULL;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < 40; ++i) {
        ShapeSlot *s = &a->shapes[i];
        if (s->texture && s->w == w && s->h == h && s->radius == radius && s->kind == kind) {
            s->used = a->frames;
            return s->texture;
        }
        if (!s->texture) {
            slot = s;
            oldest = 0;
        } else if (oldest && s->used < oldest) {
            slot = s;
            oldest = s->used;
        }
    }
    if (!slot)
        return NULL;
    if (slot->texture)
        SDL_DestroyTexture(slot->texture);
    *slot = (ShapeSlot){.w = w, .h = h, .radius = radius, .kind = kind, .used = a->frames};
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
        return NULL;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float qx = fabsf(x + .5f - w * .5f) - (w * .5f - radius),
                  qy = fabsf(y + .5f - h * .5f) - (h * .5f - radius);
            float px = fmaxf(qx, 0), py = fmaxf(qy, 0);
            float distance = sqrtf(px * px + py * py) + fminf(fmaxf(qx, qy), 0) - radius;
            float alpha = coverage(distance);
            if (kind == 1)
                alpha = 1 - alpha;
            if (kind >= 2)
                alpha -= coverage(distance + kind);
            Uint32 *pixel = (Uint32 *)((unsigned char *)surface->pixels + y * surface->pitch) + x;
            *pixel =
                SDL_MapRGBA(surface->format, 255, 255, 255, (Uint8)(fmaxf(0, alpha) * 255 + .5f));
        }
    slot->texture = SDL_CreateTextureFromSurface(a->renderer, surface);
    SDL_FreeSurface(surface);
    if (slot->texture)
        SDL_SetTextureBlendMode(slot->texture, SDL_BLENDMODE_BLEND);
    return slot->texture;
}
static void shape(App *a, SDL_Rect rect, int radius, int kind, SDL_Color c) {
    if (radius > rect.w / 2)
        radius = rect.w / 2;
    if (radius > rect.h / 2)
        radius = rect.h / 2;
    SDL_Texture *texture = mask(a, rect.w, rect.h, radius, kind);
    if (!texture)
        return;
    SDL_SetTextureColorMod(texture, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(texture, c.a);
    SDL_RenderCopy(a->renderer, texture, NULL, &rect);
}
void paint_round(App *a, SDL_Rect rect, int radius, SDL_Color color) {
    shape(a, rect, radius, 0, color);
}
void paint_ring(App *a, SDL_Rect rect, int radius, int stroke, SDL_Color color) {
    shape(a, rect, radius, stroke < 2 ? 2 : 3, color);
}
void paint_clip_corners(App *a, SDL_Rect rect, int radius, SDL_Color background) {
    shape(a, rect, radius, 1, background);
}
void paint_icon(App *a, int i, SDL_Rect rect, SDL_Color color) {
    if (i < 0 || i >= ICON_COUNT || !a->icons[i])
        return;
    SDL_SetTextureColorMod(a->icons[i], color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(a->icons[i], color.a);
    SDL_RenderCopy(a->renderer, a->icons[i], NULL, &rect);
}
bool paint_init(App *a, const char *assets) {
    const char *names[] = {"photo", "album", "heart", "play", "settings", "heart-fill", "brand"};
    for (int i = 0; i < ICON_COUNT; ++i) {
        char path[1400];
        snprintf(path, sizeof path, "%s/icons/%s.svg", assets, names[i]);
        SDL_Surface *surface = vector_surface(path, i == 6 ? 192 : 96);
        if (!surface)
            return false;
        a->icons[i] = SDL_CreateTextureFromSurface(a->renderer, surface);
        SDL_FreeSurface(surface);
        if (!a->icons[i])
            return false;
    }
    return true;
}
void paint_destroy(App *a) {
    for (int i = 0; i < ICON_COUNT; ++i)
        if (a->icons[i])
            SDL_DestroyTexture(a->icons[i]);
    for (int i = 0; i < 40; ++i)
        if (a->shapes[i].texture)
            SDL_DestroyTexture(a->shapes[i].texture);
}
