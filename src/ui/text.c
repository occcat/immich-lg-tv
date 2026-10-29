#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

TextSlot *ui_text_slot(App *a, const char *s, int size, SDL_Color c) {
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

void ui_label(App *a, const char *s, int x, int y, int size, SDL_Color c, int max_width) {
    TextSlot *t = ui_text_slot(a, s, size, c);
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

void ui_label_box(App *a, const char *s, SDL_Rect box, int size, SDL_Color c, int align) {
    TextSlot *t = ui_text_slot(a, s, size, c);
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
        t = ui_text_slot(a, fitted, size, c);
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
