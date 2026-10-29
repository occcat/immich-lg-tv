#ifndef IMMICH_LG_TV_UI_INTERNAL_H
#define IMMICH_LG_TV_UI_INTERNAL_H
#include "ui.h"

typedef struct {
    int columns, rows, width, height, step_x, step_y;
} GridLayout;
extern const SDL_Color UI_WHITE, UI_MUTED, UI_BLUE, UI_BG;
extern const int UI_FONT_SIZES[8];
extern const char *const UI_NAV_NAMES[5];
extern const char *const UI_KEYS_LOWER, *const UI_KEYS_UPPER;

GridLayout ui_grid(const App *a);
SDL_Rect ui_card(GridLayout g, int visible_index);
SDL_Rect ui_nav_rect(int index);
SDL_Rect ui_setting_rect(int index);
bool ui_contains(SDL_Rect r, int x, int y);
TextSlot *ui_text_slot(App *a, const char *s, int size, SDL_Color c);
void ui_label(App *a, const char *s, int x, int y, int size, SDL_Color c, int max_width);
void ui_label_box(App *a, const char *s, SDL_Rect box, int size, SDL_Color c, int align);
void ui_fill(App *a, int x, int y, int w, int h, SDL_Color color);
void ui_round(App *a, int x, int y, int w, int h, int radius, SDL_Color color);
void ui_outline(App *a, SDL_Rect r, SDL_Color c, int width);
void ui_image_draw(App *a, ImageSlot *slot, SDL_Rect dst, bool contain);
void ui_circle(App *a, int x, int y, int r, SDL_Color c);
void ui_logo(App *a, int x, int y, int r);
void ui_nav_icon(App *a, int i, int x, int y, SDL_Color c);
ImageSlot *ui_image_get(App *a, const char *id, bool preview);
void ui_images_clear(App *a);
bool ui_init(App *a, const char *assets, bool hidden, char *error);
void ui_destroy(App *a);
void app_load(App *a, bool reset);
void app_results(App *a);
int app_count(App *a);
bool app_albums(App *a);
void app_next_page(App *a, int direction);
void app_viewer_change(App *a, int direction);
void app_open_selected(App *a);
void app_back(App *a);
void app_tick(App *a, uint32_t now);
void app_begin_edit(App *a, int field);
void app_erase_character(char *s);
void app_edit_text(App *a, const char *s);
void app_setting_activate(App *a);
void ui_key(App *a, SDL_Keycode key, unsigned scan, bool repeat);
void ui_pointer(App *a, int x, int y, bool click);
void ui_draw_sidebar(App *a);
void ui_draw_header(App *a, const char *title, const char *subtitle);
int ui_key_hint(App *a, int x, const char *key, const char *label, int key_width, int width);
void ui_draw_footer(App *a, bool settings);
void ui_draw_gallery(App *a, uint32_t now);
void ui_draw_viewer(App *a, uint32_t now);
void ui_draw_settings(App *a);
void ui_draw_keyboard(App *a);
void ui_draw(App *a, uint32_t now);
bool ui_capture(App *a, const char *path);
#endif
