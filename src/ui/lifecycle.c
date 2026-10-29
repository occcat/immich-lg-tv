#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    a->window = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
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
        a->fonts[i] = TTF_OpenFont(font, UI_FONT_SIZES[i]);
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
    ui_images_clear(a);
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
