#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

GridLayout ui_grid(const App *a) {
    bool large = (a->view == VIEW_ALBUMS && !*a->album) || a->view == VIEW_VIDEOS;
    return large ? (GridLayout){3, 2, 496, 248, 520, 336}
                 : (GridLayout){5, 3, 288, 162, 312, 224};
}

SDL_Rect ui_card(GridLayout g, int visible_index) {
    return (SDL_Rect){304 + visible_index % g.columns * g.step_x,
                      312 + visible_index / g.columns * g.step_y, g.width, g.height};
}

SDL_Rect ui_nav_rect(int index) {
    return (SDL_Rect){24, index == 4 ? 808 : 248 + index * 90, 192, 68};
}

SDL_Rect ui_setting_rect(int index) {
    const SDL_Rect rects[] = {{336, 300, 936, 100}, {336, 424, 936, 100},
                             {336, 552, 936, 72},  {336, 640, 936, 72},
                             {336, 760, 936, 76},  {336, 878, 936, 60},
                             {1360, 760, 480, 108}};
    return rects[index];
}

bool ui_contains(SDL_Rect r, int x, int y) {
    SDL_Point point = {x, y};
    return SDL_PointInRect(&point, &r);
}
