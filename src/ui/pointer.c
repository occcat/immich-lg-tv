#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
            if (ui_contains(ui_setting_rect(i), x, y)) {
                a->setting_focus = i;
                if (click)
                    app_setting_activate(a);
                return;
            }
    }
    for (int i = 0; i < 5; ++i)
        if (ui_contains(ui_nav_rect(i), x, y)) {
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
    GridLayout g = ui_grid(a);
    for (int i = 0; i < g.columns * g.rows; ++i) {
        SDL_Rect hit = ui_card(g, i);
        hit.h += 48;
        int index = a->scroll_row * g.columns + i;
        if (index < app_count(a) && ui_contains(hit, x, y)) {
            a->selected = index;
            a->sidebar = false;
            if (click)
                app_open_selected(a);
            return;
        }
    }
}
