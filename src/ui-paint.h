#ifndef IMMICH_TV_PAINT_H
#define IMMICH_TV_PAINT_H
#include "ui.h"
bool paint_init(App *app, const char *assets);
void paint_destroy(App *app);
void paint_round(App *app, SDL_Rect rect, int radius, SDL_Color color);
void paint_ring(App *app, SDL_Rect rect, int radius, int stroke, SDL_Color color);
void paint_clip_corners(App *app, SDL_Rect rect, int radius, SDL_Color background);
void paint_icon(App *app, int icon, SDL_Rect rect, SDL_Color color);
#endif
