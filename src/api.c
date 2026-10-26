#include "api.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    void *handle;
    CURL *(*init)(void);
    CURLcode (*setopt)(CURL *, CURLoption, ...);
    CURLcode (*perform)(CURL *);
    CURLcode (*getinfo)(CURL *, CURLINFO, ...);
    void (*cleanup)(CURL *);
    CURLcode (*global_init)(long);
    void (*global_cleanup)(void);
    struct curl_slist *(*append)(struct curl_slist *, const char *);
    void (*free_all)(struct curl_slist *);
} http;
static char ca_path[1024];

bool api_init(char *error) {
    const char *names[] = {
#ifdef __APPLE__
        "/usr/lib/libcurl.4.dylib", "libcurl.dylib",
#else
        "libcurl.so.4", "libcurl.so.5", "libcurl.so",
#endif
        NULL};
    for (int i = 0; names[i] && !http.handle; ++i)
        http.handle = dlopen(names[i], RTLD_NOW | RTLD_LOCAL);
    if (!http.handle)
        goto fail;
#define LOAD(member, name)                                                                         \
    do {                                                                                           \
        *(void **)(&http.member) = dlsym(http.handle, name);                                       \
        if (!http.member)                                                                          \
            goto fail;                                                                             \
    } while (0)
    LOAD(init, "curl_easy_init");
    LOAD(setopt, "curl_easy_setopt");
    LOAD(perform, "curl_easy_perform");
    LOAD(getinfo, "curl_easy_getinfo");
    LOAD(cleanup, "curl_easy_cleanup");
    LOAD(global_init, "curl_global_init");
    LOAD(global_cleanup, "curl_global_cleanup");
    LOAD(append, "curl_slist_append");
    LOAD(free_all, "curl_slist_free_all");
#undef LOAD
    if (http.global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        goto fail;
    return true;
fail:
    copy_text(error, ERROR_SIZE, "无法初始化网络组件");
    return false;
}
void api_set_ca(const char *path) {
    copy_text(ca_path, sizeof ca_path, path);
}
void api_cleanup(void) {
    if (http.global_cleanup)
        http.global_cleanup();
    if (http.handle)
        dlclose(http.handle);
    memset(&http, 0, sizeof http);
}

typedef struct {
    unsigned char *data;
    size_t size, limit;
} Buffer;
static size_t collect(char *data, size_t size, size_t count, void *ctx) {
    Buffer *b = ctx;
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t n = size * count;
    if (n > b->limit - b->size)
        return 0;
    unsigned char *p = realloc(b->data, b->size + n + 1);
    if (!p)
        return 0;
    b->data = p;
    memcpy(b->data + b->size, data, n);
    b->size += n;
    b->data[b->size] = 0;
    return n;
}

static CURL *prepare(const Config *config, const char *method, const char *path,
                     struct curl_slist **headers, char *error) {
    if (!read_request_allowed(method, path)) {
        copy_text(error, ERROR_SIZE, "只读策略阻止了这个请求");
        return NULL;
    }
    char normalized[768], url[2048];
    if (!normalize_server(config->server, normalized, sizeof normalized, error))
        return NULL;
    if (!strncmp(normalized, "http://", 7) && !config->allow_http) {
        copy_text(error, ERROR_SIZE, "这是 HTTP 地址，请在连接设置中明确允许未加密连接");
        return NULL;
    }
    if (!*config->api_key || strpbrk(config->api_key, "\r\n")) {
        copy_text(error, ERROR_SIZE, "请输入有效的只读 API 密钥");
        return NULL;
    }
    if (snprintf(url, sizeof url, "%s/api%s", normalized, path) >= (int)sizeof url)
        return NULL;
    CURL *c = http.init();
    if (!c)
        return NULL;
    char authorization[600];
    snprintf(authorization, sizeof authorization, "x-api-key: %s", config->api_key);
    *headers = http.append(NULL, authorization);
    *headers = http.append(*headers, "Accept: application/json, image/*, video/*");
    *headers = http.append(*headers, "Content-Type: application/json");
    memset(authorization, 0, sizeof authorization);
    http.setopt(c, CURLOPT_URL, url);
    http.setopt(c, CURLOPT_HTTPHEADER, *headers);
    http.setopt(c, CURLOPT_NOSIGNAL, 1L);
    http.setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
    http.setopt(c, CURLOPT_TIMEOUT, 25L);
    http.setopt(c, CURLOPT_LOW_SPEED_LIMIT, 100L);
    http.setopt(c, CURLOPT_LOW_SPEED_TIME, 15L);
    /* Credentials never follow redirects to another host. Configure the final URL. */
    http.setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    http.setopt(c, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    http.setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    http.setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    if (*ca_path)
        http.setopt(c, CURLOPT_CAINFO, ca_path);
    http.setopt(c, CURLOPT_USERAGENT, "ImmichTV/" APP_VERSION);
    if (!strcmp(method, "HEAD"))
        http.setopt(c, CURLOPT_NOBODY, 1L);
    return c;
}
static bool status_ok(CURL *c, CURLcode result, char *error) {
    long status = 0;
    http.getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (result != CURLE_OK) {
        copy_text(error, ERROR_SIZE,
                  result == CURLE_PEER_FAILED_VERIFICATION
                      ? "HTTPS 证书验证失败，请检查证书及电视时间"
                      : "连接中断或超时，请检查服务地址与网络");
        return false;
    }
    if (status >= 200 && status < 300)
        return true;
    if (status == 401)
        copy_text(error, ERROR_SIZE, "API 密钥无效或已失效，请重新连接");
    else if (status == 403)
        copy_text(error, ERROR_SIZE, "API 密钥缺少只读权限，请检查权限设置");
    else if (status >= 300 && status < 400)
        copy_text(error, ERROR_SIZE, "服务返回重定向，请填写重定向后的最终地址");
    else
        snprintf(error, ERROR_SIZE, "服务返回 HTTP %ld，请检查 Immich 版本与地址", status);
    return false;
}
cJSON *api_json(const Config *config, const char *method, const char *path, const char *body,
                char *error) {
    struct curl_slist *headers = NULL;
    CURL *c = prepare(config, method, path, &headers, error);
    if (!c)
        return NULL;
    Buffer b = {.limit = 8 * 1024 * 1024};
    http.setopt(c, CURLOPT_WRITEFUNCTION, collect);
    http.setopt(c, CURLOPT_WRITEDATA, &b);
    if (body) {
        http.setopt(c, CURLOPT_POSTFIELDS, body);
        http.setopt(c, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }
    CURLcode result = http.perform(c);
    cJSON *json = NULL;
    if (status_ok(c, result, error)) {
        json = cJSON_ParseWithLength((const char *)b.data, b.size);
        if (!json)
            copy_text(error, ERROR_SIZE, "服务返回了无效的 JSON 数据");
    }
    free(b.data);
    http.cleanup(c);
    http.free_all(headers);
    return json;
}
unsigned char *api_image(const Config *config, const char *id, bool preview, size_t *length,
                         char *error) {
    if (!valid_asset_id(id))
        return NULL;
    char path[200];
    snprintf(path, sizeof path, "/assets/%s/thumbnail?size=%s", id,
             preview ? "preview" : "thumbnail");
    struct curl_slist *headers = NULL;
    CURL *c = prepare(config, "GET", path, &headers, error);
    if (!c)
        return NULL;
    Buffer b = {.limit = 24 * 1024 * 1024};
    http.setopt(c, CURLOPT_WRITEFUNCTION, collect);
    http.setopt(c, CURLOPT_WRITEDATA, &b);
    CURLcode result = http.perform(c);
    bool ok = status_ok(c, result, error);
    http.cleanup(c);
    http.free_all(headers);
    if (!ok) {
        free(b.data);
        return NULL;
    }
    *length = b.size;
    return b.data;
}

Catalog *api_catalog(const Config *config, View view, const char *album, int page,
                     const char *cursor, char *error) {
    Catalog *out = calloc(1, sizeof *out);
    if (!out)
        return NULL;
    cJSON *version = api_json(config, "GET", "/server/version", NULL, error);
    if (!version) {
        catalog_free(out);
        return NULL;
    }
    out->server_major = json_integer(version, "major", 2);
    out->server_minor = json_integer(version, "minor", 0);
    cJSON_Delete(version);
    if (view == VIEW_ALBUMS && (!album || !*album)) {
        cJSON *j = api_json(config, "GET", "/albums", NULL, error);
        bool ok = j && parse_albums(j, out, error);
        cJSON_Delete(j);
        if (!ok) {
            catalog_free(out);
            return NULL;
        }
        return out;
    }
    /* Search is a POST, but is a read-only operation. Album results are paginated too. */
    cJSON *body = cJSON_CreateObject();
    cJSON_AddNumberToObject(body, "size", PAGE_SIZE);
    cJSON_AddBoolToObject(body, "withExif", true);
    if (out->server_major > 3 || (out->server_major == 3 && out->server_minor >= 2)) {
        cJSON *filter = cJSON_AddObjectToObject(body, "filter");
        cJSON *visibility = cJSON_AddObjectToObject(filter, "visibility");
        if (album && *album) {
            cJSON *allowed = cJSON_AddArrayToObject(visibility, "in");
            cJSON_AddItemToArray(allowed, cJSON_CreateString("timeline"));
            cJSON_AddItemToArray(allowed, cJSON_CreateString("archive"));
        } else
            cJSON_AddStringToObject(visibility, "eq", "timeline");
        cJSON *trash = cJSON_AddObjectToObject(filter, "trashedAt");
        cJSON_AddNullToObject(trash, "eq");
        if (view == VIEW_FAVORITES)
            cJSON_AddBoolToObject(cJSON_AddObjectToObject(filter, "isFavorite"), "eq", true);
        if (view == VIEW_VIDEOS)
            cJSON_AddStringToObject(cJSON_AddObjectToObject(filter, "type"), "eq", "VIDEO");
        if (album && *album)
            cJSON_AddItemToArray(
                cJSON_AddArrayToObject(cJSON_AddObjectToObject(filter, "albumIds"), "any"),
                cJSON_CreateString(album));
        cJSON *order = cJSON_AddObjectToObject(body, "orderBy");
        cJSON_AddStringToObject(order, "field", "fileCreatedAt");
        cJSON_AddStringToObject(order, "direction", "desc");
        if (cursor && *cursor)
            cJSON_AddStringToObject(body, "cursor", cursor);
    } else {
        cJSON_AddNumberToObject(body, "page", page);
        cJSON_AddStringToObject(body, "order", "desc");
        if (!album || !*album)
            cJSON_AddStringToObject(body, "visibility", "timeline");
        cJSON_AddBoolToObject(body, "isTrashed", false);
        if (view == VIEW_FAVORITES)
            cJSON_AddBoolToObject(body, "isFavorite", true);
        if (view == VIEW_VIDEOS)
            cJSON_AddStringToObject(body, "type", "VIDEO");
        if (album && *album)
            cJSON_AddItemToArray(cJSON_AddArrayToObject(body, "albumIds"),
                                 cJSON_CreateString(album));
    }
    char *encoded = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    cJSON *j = encoded ? api_json(config, "POST", "/search/metadata", encoded, error) : NULL;
    free(encoded);
    bool ok = j && parse_assets(j, out, error);
    cJSON_Delete(j);
    if (!ok) {
        catalog_free(out);
        return NULL;
    }
    return out;
}

typedef struct {
    void *ctx;
    int (*cancel)(void *);
} Progress;
static int stream_progress(void *data, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    Progress *p = data;
    return p->cancel ? p->cancel(p->ctx) : 0;
}
bool api_stream(const Config *config, const char *id, const char *range, bool head,
                StreamWriter header, StreamWriter body, void *ctx, int (*cancel)(void *),
                char *error) {
    if (!valid_asset_id(id))
        return false;
    if (range)
        for (const char *p = range; *p; ++p)
            if ((*p < '0' || *p > '9') && *p != '-')
                return false;
    char path[200];
    snprintf(path, sizeof path, "/assets/%s/video/playback", id);
    struct curl_slist *headers = NULL;
    CURL *c = prepare(config, head ? "HEAD" : "GET", path, &headers, error);
    if (!c)
        return false;
    if (range && *range)
        http.setopt(c, CURLOPT_RANGE, range);
    Progress p = {ctx, cancel};
    http.setopt(c, CURLOPT_TIMEOUT, 0L);
    http.setopt(c, CURLOPT_NOPROGRESS, 0L);
    http.setopt(c, CURLOPT_XFERINFOFUNCTION, stream_progress);
    http.setopt(c, CURLOPT_XFERINFODATA, &p);
    http.setopt(c, CURLOPT_HEADERFUNCTION, header);
    http.setopt(c, CURLOPT_HEADERDATA, ctx);
    http.setopt(c, CURLOPT_WRITEFUNCTION, body);
    http.setopt(c, CURLOPT_WRITEDATA, ctx);
    CURLcode result = http.perform(c);
    bool ok = status_ok(c, result, error);
    http.cleanup(c);
    http.free_all(headers);
    return ok;
}

typedef struct {
    int64_t total;
    long status;
} RangeInfo;
static size_t range_header(char *data, size_t size, size_t count, void *context) {
    size_t n = size * count;
    RangeInfo *info = context;
    if (n > 2048)
        return 0;
    char line[2049];
    memcpy(line, data, n);
    line[n] = 0;
    long long first, last, total;
    if (sscanf(line, "Content-Range: bytes %lld-%lld/%lld", &first, &last, &total) == 3 ||
        sscanf(line, "content-range: bytes %lld-%lld/%lld", &first, &last, &total) == 3)
        info->total = total;
    else if (sscanf(line, "Content-Length: %lld", &total) == 1 && info->total <= 0)
        info->total = total;
    if (!strncmp(line, "HTTP/", 5)) {
        char *p = strchr(line, ' ');
        info->status = p ? atol(p + 1) : 0;
    }
    return n;
}
unsigned char *api_video_chunk(const Config *config, const char *id, int64_t offset,
                               size_t capacity, size_t *length, int64_t *total, void *context,
                               int (*cancel)(void *), char *error) {
    if (!valid_asset_id(id) || offset < 0 || capacity > 4 * 1024 * 1024)
        return NULL;
    char path[200], range[100];
    snprintf(path, sizeof path, "/assets/%s/video/playback", id);
    snprintf(range, sizeof range, "%lld-%lld", (long long)offset,
             (long long)(offset + (int64_t)capacity - 1));
    struct curl_slist *headers = NULL;
    CURL *c = prepare(config, "GET", path, &headers, error);
    if (!c)
        return NULL;
    Buffer b = {.limit = capacity};
    RangeInfo info = {0};
    Progress progress = {context, cancel};
    http.setopt(c, CURLOPT_RANGE, range);
    http.setopt(c, CURLOPT_WRITEFUNCTION, collect);
    http.setopt(c, CURLOPT_WRITEDATA, &b);
    http.setopt(c, CURLOPT_HEADERFUNCTION, range_header);
    http.setopt(c, CURLOPT_HEADERDATA, &info);
    http.setopt(c, CURLOPT_NOPROGRESS, 0L);
    http.setopt(c, CURLOPT_XFERINFOFUNCTION, stream_progress);
    http.setopt(c, CURLOPT_XFERINFODATA, &progress);
    CURLcode result = http.perform(c);
    bool ok = status_ok(c, result, error);
    if (ok && info.status != 206 && offset != 0) {
        copy_text(error, ERROR_SIZE, "服务端不支持视频分段读取");
        ok = false;
    }
    http.cleanup(c);
    http.free_all(headers);
    if (!ok) {
        free(b.data);
        return NULL;
    }
    *length = b.size;
    if (info.total > 0)
        *total = info.total;
    return b.data;
}
