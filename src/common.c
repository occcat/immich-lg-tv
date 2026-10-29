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
