#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
            GridLayout g = ui_grid(a);
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
