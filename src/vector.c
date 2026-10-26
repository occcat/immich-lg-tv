#include "vector.h"
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"
#pragma GCC diagnostic pop

SDL_Surface *vector_surface(const char *path, int pixels) {
    NSVGimage *image = nsvgParseFromFile(path, "px", 96);
    if (!image || image->width <= 0 || image->height <= 0) {
        nsvgDelete(image);
        return NULL;
    }
    SDL_Surface *surface =
        SDL_CreateRGBSurfaceWithFormat(0, pixels, pixels, 32, SDL_PIXELFORMAT_RGBA32);
    NSVGrasterizer *raster = nsvgCreateRasterizer();
    if (surface && raster)
        nsvgRasterize(raster, image, 0, 0, pixels / image->width, surface->pixels, pixels, pixels,
                      surface->pitch);
    else {
        SDL_FreeSurface(surface);
        surface = NULL;
    }
    nsvgDeleteRasterizer(raster);
    nsvgDelete(image);
    return surface;
}
