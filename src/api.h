#ifndef IMMICH_TV_API_H
#define IMMICH_TV_API_H
#include "core.h"
#include <curl/curl.h>
typedef size_t (*StreamWriter)(char *, size_t, size_t, void *);
bool api_init(char *error);
void api_cleanup(void);
void api_set_ca(const char *path);
cJSON *api_json(const Config *config, const char *method, const char *path, const char *body,
                char *error);
unsigned char *api_image(const Config *config, const char *id, bool preview, size_t *length,
                         char *error);
Catalog *api_catalog(const Config *config, View view, const char *album, int page,
                     const char *cursor, char *error);
bool api_stream(const Config *config, const char *id, const char *range, bool head,
                StreamWriter header, StreamWriter body, void *ctx, int (*cancel)(void *),
                char *error);
unsigned char *api_video_chunk(const Config *config, const char *id, int64_t offset,
                               size_t capacity, size_t *length, int64_t *total, void *context,
                               int (*cancel)(void *), char *error);
#endif
