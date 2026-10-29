#ifndef IMMICH_LG_TV_UI_H
#define IMMICH_LG_TV_UI_H
#include "app.h"

bool ui_init(App *app, const char *assets, bool hidden, char *error);
void ui_destroy(App *app);
void ui_draw(App *app, uint32_t now);
void ui_key(App *app, SDL_Keycode key, unsigned scancode, bool repeat);
void ui_pointer(App *app, int x, int y, bool click);
bool ui_capture(App *app, const char *path);
#endif
