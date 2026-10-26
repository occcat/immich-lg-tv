#include "core.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void copy_text(char *dst, size_t cap, const char *src) {
    if (!cap)
        return;
    snprintf(dst, cap, "%s", src ? src : "");
    /* Never leave an incomplete UTF-8 sequence at a truncation boundary. */
    size_t n = strlen(dst);
    if (src && strlen(src) >= cap && n) {
        size_t i = n;
        while (i && (((unsigned char)dst[i - 1] & 0xc0) == 0x80))
            --i;
        if (i && (unsigned char)dst[i - 1] >= 0xc0)
            dst[i - 1] = 0;
    }
}

const char *json_text(const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
int json_integer(const cJSON *obj, const char *key, int fallback) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valueint : fallback;
}
bool valid_asset_id(const char *id) {
    if (!id || !*id || strlen(id) >= 80)
        return false;
    for (const unsigned char *p = (const unsigned char *)id; *p; ++p)
        if (!isalnum(*p) && *p != '-')
            return false;
    return true;
}

bool normalize_server(const char *input, char *out, size_t cap, char *error) {
    if (!input || strlen(input) >= cap || strlen(input) < 8)
        goto invalid;
    const char *host = NULL;
    if (!strncmp(input, "https://", 8))
        host = input + 8;
    else if (!strncmp(input, "http://", 7))
        host = input + 7;
    else
        goto invalid;
    if (!*host || *host == '/' || *host == ':' || strpbrk(input, "@?#\\\r\n\t ") ||
        strstr(host, ".."))
        goto invalid;
    copy_text(out, cap, input);
    size_t len = strlen(out);
    while (len && out[len - 1] == '/')
        out[--len] = 0;
    if (len >= 4 && !strcmp(out + len - 4, "/api"))
        out[len -= 4] = 0;
    if (len <= (size_t)(host - input))
        goto invalid;
    return true;
invalid:
    copy_text(error, ERROR_SIZE, "请输入有效的 http:// 或 https:// 服务地址（支持子路径）");
    return false;
}

bool read_request_allowed(const char *method, const char *path) {
    if (!method || !path || path[0] != '/' || strpbrk(path, "\\\r\n#") || strstr(path, "..") ||
        strchr(path, '%'))
        return false;
    char clean[256];
    size_t n = strcspn(path, "?");
    if (n >= sizeof clean)
        return false;
    memcpy(clean, path, n);
    clean[n] = 0;
    if (!strcmp(method, "POST"))
        return !strcmp(clean, "/search/metadata");
    if (strcmp(method, "GET") && strcmp(method, "HEAD"))
        return false;
    bool head = !strcmp(method, "HEAD");
    if (!head && (!strcmp(clean, "/server/version") || !strcmp(clean, "/server/ping") ||
                  !strcmp(clean, "/albums")))
        return true;
    if (!strncmp(clean, "/albums/", 8) && !head)
        return valid_asset_id(clean + 8);
    if (!strncmp(clean, "/assets/", 8)) {
        char id[80];
        const char *tail = strchr(clean + 8, '/');
        if (!tail)
            return !head && valid_asset_id(clean + 8);
        size_t z = (size_t)(tail - clean - 8);
        if (!z || z >= sizeof id)
            return false;
        memcpy(id, clean + 8, z);
        id[z] = 0;
        if (!valid_asset_id(id))
            return false;
        return !strcmp(tail, "/thumbnail") || !strcmp(tail, "/video/playback");
    }
    return false;
}

bool config_load(const char *path, Config *config) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return false;
    struct stat st;
    char buf[4096];
    bool safe = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == getuid() &&
                !(st.st_mode & 0077) && st.st_size < (off_t)sizeof buf;
    ssize_t n = safe ? read(fd, buf, sizeof buf - 1) : -1;
    close(fd);
    if (n <= 0)
        return false;
    buf[n] = 0;
    cJSON *j = cJSON_Parse(buf);
    if (!j)
        return false;
    char normalized[768], error[ERROR_SIZE];
    bool ok = normalize_server(json_text(j, "server"), normalized, sizeof normalized, error);
    if (ok) {
        copy_text(config->server, sizeof config->server, normalized);
        copy_text(config->api_key, sizeof config->api_key, json_text(j, "apiKey"));
        config->allow_http = cJSON_IsTrue(cJSON_GetObjectItem(j, "allowHttp"));
        config->remember = true;
        int secs = json_integer(j, "slideshowSeconds", 8);
        config->slideshow_seconds = secs >= 3 && secs <= 60 ? secs : 8;
    }
    cJSON_Delete(j);
    memset(buf, 0, sizeof buf);
    return ok;
}

bool config_save(const char *path, const Config *config, char *error) {
    if (!path || !*path) {
        copy_text(error, ERROR_SIZE, "本机存储不可用，连接未保存");
        return false;
    }
    char temporary[1024];
    if (snprintf(temporary, sizeof temporary, "%s.XXXXXX", path) >= (int)sizeof temporary)
        return false;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "server", config->server);
    cJSON_AddStringToObject(j, "apiKey", config->api_key);
    cJSON_AddBoolToObject(j, "allowHttp", config->allow_http);
    cJSON_AddNumberToObject(j, "slideshowSeconds", config->slideshow_seconds);
    char *data = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    int fd = mkstemp(temporary);
    bool ok = false;
    if (fd >= 0 && data) {
        size_t len = strlen(data), done = 0;
        while (done < len) {
            ssize_t n = write(fd, data + done, len - done);
            if (n <= 0)
                break;
            done += (size_t)n;
        }
        ok = done == len && !fchmod(fd, 0600) && !fsync(fd);
        close(fd);
        if (ok)
            ok = rename(temporary, path) == 0;
    } else if (fd >= 0)
        close(fd);
    if (!ok) {
        unlink(temporary);
        copy_text(error, ERROR_SIZE, "无法保存连接；本次连接仍可使用");
    }
    if (data) {
        memset(data, 0, strlen(data));
        free(data);
    }
    return ok;
}
void config_forget(const char *path) {
    if (path && *path)
        unlink(path);
}

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
int grid_move(int current, int dx, int dy, int columns, int count) {
    if (count <= 0 || columns <= 0)
        return -1;
    if (current < 0)
        return 0;
    int row = current / columns, col = current % columns;
    if (dx) {
        int next = current + dx;
        if (next < 0 || next >= count || next / columns != row)
            return current;
        return next;
    }
    int target = (row + dy) * columns + col;
    if (target < 0)
        return current;
    if (target >= count) {
        if ((row + dy) * columns < count)
            return count - 1;
        return current;
    }
    return target;
}
FrameRect fit_frame(int sw, int sh, int width, int height) {
    FrameRect r = {0, 0, width, height};
    if (sw <= 0 || sh <= 0 || width <= 0 || height <= 0)
        return r;
    if ((int64_t)sw * height > (int64_t)sh * width)
        r.h = (int)((int64_t)width * sh / sw);
    else
        r.w = (int)((int64_t)height * sw / sh);
    r.x = (width - r.w) / 2;
    r.y = (height - r.h) / 2;
    return r;
}
