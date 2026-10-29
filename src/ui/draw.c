#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void ui_fill(App *a, int x, int y, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawColor(a->renderer, color.r, color.g, color.b, color.a);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(a->renderer, &r);
}

void ui_round(App *a, int x, int y, int w, int h, int radius, SDL_Color color) {
    paint_round(a, (SDL_Rect){x, y, w, h}, radius, color);
}

void ui_outline(App *a, SDL_Rect r, SDL_Color c, int width) {
    paint_ring(a, r, r.w > 500 ? 12 : 20, width, c);
}

void ui_image_draw(App *a, ImageSlot *slot, SDL_Rect dst, bool contain) {
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

void ui_circle(App *a, int x, int y, int r, SDL_Color c) {
    paint_round(a, (SDL_Rect){x - r, y - r, 2 * r, 2 * r}, r, c);
}

void ui_logo(App *a, int x, int y, int r) {
    paint_icon(a, 6, (SDL_Rect){x - r, y - r, 2 * r, 2 * r}, (SDL_Color){255, 255, 255, 255});
}

void ui_nav_icon(App *a, int i, int x, int y, SDL_Color c) {
    paint_icon(a, i, (SDL_Rect){x, y, 32, 32}, c);
}
