#include "core.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void catalog_free(Catalog *c) {
    if (c) {
        free(c->assets);
        free(c->albums);
        free(c);
    }
}

double parse_duration(const cJSON *v) {
    if (cJSON_IsNumber(v))
        return v->valuedouble / 1000.0;
    if (cJSON_IsString(v)) {
        double s = 0;
        int h = 0, m = 0;
        if (sscanf(v->valuestring, "%d:%d:%lf", &h, &m, &s) == 3)
            return h * 3600.0 + m * 60.0 + s;
    }
    return 0;
}

void asset_parse(const cJSON *j, Asset *a) {
    memset(a, 0, sizeof *a);
    copy_text(a->id, sizeof a->id, json_text(j, "id"));
    copy_text(a->name, sizeof a->name, json_text(j, "originalFileName"));
    const char *date = json_text(j, "localDateTime");
    if (!*date)
        date = json_text(j, "fileCreatedAt");
    copy_text(a->date, sizeof a->date, date);
    a->video = !strcmp(json_text(j, "type"), "VIDEO");
    a->favorite = cJSON_IsTrue(cJSON_GetObjectItem(j, "isFavorite"));
    const cJSON *exif = cJSON_GetObjectItem(j, "exifInfo");
    copy_text(a->city, sizeof a->city, json_text(exif, "city"));
    copy_text(a->camera, sizeof a->camera, json_text(exif, "model"));
    a->width = json_integer(j, "width", json_integer(exif, "exifImageWidth", 0));
    a->height = json_integer(j, "height", json_integer(exif, "exifImageHeight", 0));
    a->duration = parse_duration(cJSON_GetObjectItem(j, "duration"));
}

bool parse_assets(const cJSON *j, Catalog *out, char *error) {
    const cJSON *assets = cJSON_GetObjectItem(j, "assets");
    const cJSON *items = cJSON_IsArray(assets) ? assets : cJSON_GetObjectItem(assets, "items");
    if (!cJSON_IsArray(items)) {
        copy_text(error, ERROR_SIZE, "服务返回的照片列表格式不受支持");
        return false;
    }
    size_t cap = (size_t)cJSON_GetArraySize(items);
    if (cap > 1000) {
        copy_text(error, ERROR_SIZE, "服务返回了过大的照片列表");
        return false;
    }
    out->assets = calloc(cap ? cap : 1, sizeof(Asset));
    if (!out->assets)
        return false;
    const cJSON *a = NULL;
    cJSON_ArrayForEach(a, items) {
        if (cJSON_IsTrue(cJSON_GetObjectItem(a, "isTrashed")))
            continue;
        const char *visibility = json_text(a, "visibility");
        if (!strcmp(visibility, "hidden") || !strcmp(visibility, "locked"))
            continue;
        Asset parsed;
        asset_parse(a, &parsed);
        if (valid_asset_id(parsed.id))
            out->assets[out->count++] = parsed;
    }
    if (cJSON_IsObject(assets)) {
        copy_text(out->next_cursor, sizeof out->next_cursor, json_text(assets, "nextCursor"));
        const cJSON *next = cJSON_GetObjectItem(assets, "nextPage");
        if (cJSON_IsNumber(next))
            out->next_page = next->valueint;
        else if (cJSON_IsString(next))
            out->next_page = atoi(next->valuestring);
    }
    return true;
}

bool parse_albums(const cJSON *j, Catalog *out, char *error) {
    if (!cJSON_IsArray(j) || cJSON_GetArraySize(j) > 10000) {
        copy_text(error, ERROR_SIZE, "服务返回的相册列表格式不受支持");
        return false;
    }
    size_t n = (size_t)cJSON_GetArraySize(j);
    out->albums = calloc(n ? n : 1, sizeof(Album));
    if (!out->albums)
        return false;
    const cJSON *a;
    cJSON_ArrayForEach(a, j) {
        const char *id = json_text(a, "id");
        if (!valid_asset_id(id))
            continue;
        Album *b = &out->albums[out->count++];
        copy_text(b->id, sizeof b->id, id);
        copy_text(b->name, sizeof b->name, json_text(a, "albumName"));
        copy_text(b->cover_id, sizeof b->cover_id, json_text(a, "albumThumbnailAssetId"));
        copy_text(b->description, sizeof b->description, json_text(a, "description"));
        b->count = json_integer(a, "assetCount", 0);
    }
    return true;
}
