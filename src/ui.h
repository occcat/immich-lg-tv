#ifndef IMMICH_TV_UI_H
#define IMMICH_TV_UI_H
#include "worker.h"
#include <SDL.h>
#include <SDL_ttf.h>
#define IMAGE_CACHE 36
#define TEXT_CACHE 256
#define ICON_COUNT 7
typedef struct {
    SDL_Texture *texture;
    int w, h, radius, kind;
    uint32_t used;
} ShapeSlot;
typedef struct {
    char id[80];
    bool preview, pending, failed;
    SDL_Texture *texture;
    int w, h;
    uint32_t used;
} ImageSlot;
typedef struct {
    char text[640];
    int size;
    SDL_Color color;
    SDL_Texture *texture;
    SDL_Rect ink;
    int w, h;
    int fitted_width;
    char fitted[640];
    uint32_t used;
} TextSlot;
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *shade_vertical;
    SDL_Texture *icons[ICON_COUNT];
    ShapeSlot shapes[40];
    TTF_Font *fonts[8];
    TextSlot text[TEXT_CACHE];
    ImageSlot images[IMAGE_CACHE];
    Config config, editing;
    Catalog *catalog;
    unsigned generation;
    View view;
    int nav, selected, scroll_row, page, page_count;
    char **cursors;
    int *pages;
    char album[80], album_name[320], error[ERROR_SIZE], notice[ERROR_SIZE], config_path[1024];
    bool loading, sidebar, settings, viewer, info, slideshow, quit, video, paused, connected;
    int setting_focus, keyboard_index, keyboard_field;
    bool keyboard, shift, exit_confirm;
    char keyboard_before[768];
    uint32_t last_slide, last_input, frames;
    double zoom;
    float focus_x, focus_y;
    bool focus_valid;
} App;
bool ui_init(App *app, const char *assets, bool hidden, char *error);
void ui_destroy(App *app);
void ui_draw(App *app, uint32_t now);
void ui_key(App *app, SDL_Keycode key, unsigned scancode, bool repeat);
void ui_pointer(App *app, int x, int y, bool click);
void ui_text(App *app, const char *text);
void app_load(App *app, bool reset_page);
void app_results(App *app);
void app_open_selected(App *app);
void app_back(App *app);
void app_tick(App *app, uint32_t now);
bool ui_capture(App *app, const char *path);
#endif
