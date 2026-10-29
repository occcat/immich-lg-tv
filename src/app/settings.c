#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void app_begin_edit(App *a, int field) {
    a->keyboard = true;
    a->keyboard_field = field;
    a->keyboard_index = 0;
    copy_text(a->keyboard_before, sizeof a->keyboard_before,
              field ? a->editing.api_key : a->editing.server);
    SDL_StartTextInput();
}

void app_erase_character(char *s) {
    size_t n = strlen(s);
    if (!n)
        return;
    --n;
    while (n && ((unsigned char)s[n] & 0xc0) == 0x80)
        --n;
    s[n] = 0;
}

void app_edit_text(App *a, const char *s) {
    if (!a->keyboard)
        return;
    char *dst = a->keyboard_field ? a->editing.api_key : a->editing.server;
    size_t cap = a->keyboard_field ? sizeof a->editing.api_key : sizeof a->editing.server;
    size_t n = strlen(dst);
    if (n + strlen(s) < cap && !strpbrk(s, "\r\n"))
        strcat(dst, s);
}

void app_setting_activate(App *a) {
    if (a->setting_focus < 2) {
        app_begin_edit(a, a->setting_focus);
        return;
    }
    if (a->setting_focus == 2)
        a->editing.allow_http = !a->editing.allow_http;
    if (a->setting_focus == 3)
        a->editing.remember = !a->editing.remember;
    if (a->setting_focus == 4) {
        char normalized[768];
        if (!normalize_server(a->editing.server, normalized, sizeof normalized, a->error))
            return;
        if (!*a->editing.api_key) {
            copy_text(a->error, sizeof a->error, "请填写 API 密钥");
            return;
        }
        copy_text(a->editing.server, sizeof a->editing.server, normalized);
        a->config = a->editing;
        a->settings = false;
        a->view = VIEW_TIMELINE;
        a->nav = 0;
        a->album[0] = 0;
        a->sidebar = false;
        app_load(a, true);
    }
    if (a->setting_focus == 5) {
        config_forget(a->config_path);
        memset(a->editing.api_key, 0, sizeof a->editing.api_key);
        memset(a->config.api_key, 0, sizeof a->config.api_key);
        catalog_free(a->catalog);
        a->catalog = NULL;
        a->connected = false;
        a->settings = true;
        worker_cancel_pending();
        ++a->generation;
        ui_images_clear(a);
        copy_text(a->notice, sizeof a->notice, "本机保存的连接已清除");
    }
    if (a->setting_focus == 6) {
        a->editing.slideshow_seconds += 3;
        if (a->editing.slideshow_seconds > 20)
            a->editing.slideshow_seconds = 5;
        a->config.slideshow_seconds = a->editing.slideshow_seconds;
        if (a->config.remember) {
            char e[ERROR_SIZE];
            config_save(a->config_path, &a->config, e);
        }
    }
}
