#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
            app_erase_character(a->keyboard_field ? a->editing.api_key : a->editing.server);
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
            const char *k = a->shift ? UI_KEYS_UPPER : UI_KEYS_LOWER;
            if (i < (int)strlen(k)) {
                char s[] = {k[i], 0};
                app_edit_text(a, s);
            } else if (i == 55)
                a->shift = !a->shift;
            else if (i == 56)
                app_erase_character(a->keyboard_field ? a->editing.api_key : a->editing.server);
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
            app_setting_activate(a);
        return;
    }
    if (a->viewer) {
        Asset *asset =
            a->catalog && a->selected < app_count(a) ? &a->catalog->assets[a->selected] : NULL;
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
            app_viewer_change(a, -1);
        if (right)
            app_viewer_change(a, 1);
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
        app_next_page(a, -1);
        return;
    }
    if (key == SDLK_PAGEDOWN || scan == 301) {
        app_next_page(a, 1);
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
    if (a->loading || !app_count(a)) {
        if (left)
            a->sidebar = true;
        if (ok && !repeat)
            app_load(a, false);
        return;
    }
    GridLayout g = ui_grid(a);
    if (left && a->selected % g.columns == 0) {
        a->sidebar = true;
        return;
    }
    int selected = a->selected;
    if (left)
        selected = grid_move(selected, -1, 0, g.columns, app_count(a));
    if (right)
        selected = grid_move(selected, 1, 0, g.columns, app_count(a));
    if (up)
        selected = grid_move(selected, 0, -1, g.columns, app_count(a));
    if (down)
        selected = grid_move(selected, 0, 1, g.columns, app_count(a));
    if (down && selected == a->selected && a->selected / g.columns == (app_count(a) - 1) / g.columns) {
        app_next_page(a, 1);
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
    if (play && !repeat && !app_albums(a)) {
        app_open_selected(a);
        a->slideshow = true;
        a->last_slide = SDL_GetTicks();
    }
}
