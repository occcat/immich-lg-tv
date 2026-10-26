/* Desktop preview uses an external player. The television adapter is in media-webos.c. */
#ifndef WEBOS
#include "media.h"
#include "proxy.h"
#include <SDL.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
static pid_t child;
static bool active, held;
static uint32_t began;
static double offset;
bool media_start(const Config *config, const Asset *asset, char *error) {
    media_stop();
    char url[200];
    if (!proxy_start(config, asset->id, url, sizeof url, error))
        return false;
    char *args[] = {"ffplay",        "-loglevel",        "error", "-autoexit",
                    "-window_title", "Immich TV · 视频", url,     NULL};
    if (posix_spawnp(&child, "ffplay", NULL, NULL, args, environ)) {
        copy_text(error, ERROR_SIZE, "桌面视频预览需要 ffmpeg（ffplay）");
        proxy_stop();
        return false;
    }
    active = true;
    held = false;
    offset = 0;
    began = SDL_GetTicks();
    return true;
}
void media_stop(void) {
    if (child > 0) {
        kill(child, SIGCONT);
        kill(child, SIGTERM);
        waitpid(child, NULL, 0);
        child = 0;
    }
    proxy_stop();
    active = held = false;
}
double media_position(void) {
    return offset + ((active && !held) ? (SDL_GetTicks() - began) / 1000.0 : 0);
}
void media_pause(bool paused) {
    offset = media_position();
    began = SDL_GetTicks();
    held = paused;
    if (child > 0)
        kill(child, paused ? SIGSTOP : SIGCONT);
}
void media_seek(double seconds) {
    (void)seconds; /* ffplay owns desktop keyboard seeking. */
}
int media_state(void) {
    if (active && child > 0 && waitpid(child, NULL, WNOHANG) == child) {
        child = 0;
        return 2;
    }
    return active ? 1 : 0;
}
const char *media_error(void) {
    return "桌面播放器未能播放这段视频";
}
void media_shutdown(void) {
    media_stop();
}
#endif
