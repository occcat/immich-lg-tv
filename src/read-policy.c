#include "core.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
