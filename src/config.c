#include "core.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
