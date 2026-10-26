#ifndef IMMICH_TV_CORE_H
#define IMMICH_TV_CORE_H
#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_ID "com.immichtv.viewer"
#define APP_VERSION "0.1.0"
#define PAGE_SIZE 40
#define ERROR_SIZE 256

typedef struct {
    char server[768];
    char api_key[512];
    bool allow_http;
    bool remember;
    int slideshow_seconds;
} Config;

typedef struct {
    char id[80], name[320], date[48], city[160], camera[160];
    bool video, favorite;
    int width, height;
    double duration;
} Asset;

typedef struct {
    char id[80], name[320], cover_id[80], description[512];
    int count;
} Album;

typedef enum { VIEW_TIMELINE, VIEW_ALBUMS, VIEW_FAVORITES, VIEW_VIDEOS } View;
typedef struct {
    Asset *assets;
    Album *albums;
    size_t count;
    char next_cursor[1024];
    int next_page;
    int server_major, server_minor;
} Catalog;
typedef struct {
    int x, y, w, h;
} FrameRect;
FrameRect fit_frame(int source_width, int source_height, int width, int height);

void copy_text(char *dst, size_t cap, const char *src);
const char *json_text(const cJSON *obj, const char *key);
int json_integer(const cJSON *obj, const char *key, int fallback);
bool valid_asset_id(const char *id);
bool normalize_server(const char *input, char *out, size_t cap, char *error);
bool read_request_allowed(const char *method, const char *path);
bool config_load(const char *path, Config *config);
bool config_save(const char *path, const Config *config, char *error);
void config_forget(const char *path);
void catalog_free(Catalog *catalog);
bool parse_assets(const cJSON *json, Catalog *out, char *error);
bool parse_albums(const cJSON *json, Catalog *out, char *error);
void asset_parse(const cJSON *json, Asset *out);
int grid_move(int current, int dx, int dy, int columns, int count);
double parse_duration(const cJSON *duration);
#endif
