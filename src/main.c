#include "media.h"
#include "ui.h"
#include <SDL_image.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;
static void interrupt_handler(int sig) {
    (void)sig;
    interrupted = 1;
}
static uint32_t read_word(const unsigned char *data, size_t offset) {
    uint32_t x;
    memcpy(&x, data + offset, sizeof x);
    return x;
}
static bool exists(const char *p) {
    return access(p, R_OK) == 0;
}

#ifdef WEBOS
extern FILE *elogf;
static void native_log(void) {
    const char *path = "/media/developer/temp/immich-tv-diagnostic.log";
    int fd = open(path, O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC, 0640);
    if (fd < 0)
        return;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != getuid() || st.st_nlink != 1) {
        close(fd);
        return;
    }
    fchmod(fd, 0640);
    dup2(fd, STDERR_FILENO);
    elogf = fdopen(fd, "a");
}
static void fatal_handler(int signal_number, siginfo_t *info, void *context) {
    (void)info;
    (void)context;
    char message[] = "fatal native signal=00\n";
    message[20] = (char)('0' + signal_number / 10);
    message[21] = (char)('0' + signal_number % 10);
    write(STDERR_FILENO, message, sizeof message - 1);
    _exit(128 + signal_number);
}
#endif

static void launch_config(const cJSON *j, App *a) {
    const char *server = json_text(j, "server"), *key = json_text(j, "apiKey");
    if (*server)
        copy_text(a->config.server, sizeof a->config.server, server);
    if (*key)
        copy_text(a->config.api_key, sizeof a->config.api_key, key);
    if (cJSON_HasObjectItem(j, "allowHttp"))
        a->config.allow_http = cJSON_IsTrue(cJSON_GetObjectItem(j, "allowHttp"));
    if (cJSON_HasObjectItem(j, "remember"))
        a->config.remember = cJSON_IsTrue(cJSON_GetObjectItem(j, "remember"));
    int secs = json_integer(j, "slideshowSeconds", a->config.slideshow_seconds);
    if (secs >= 3 && secs <= 60)
        a->config.slideshow_seconds = secs;
}
#ifdef ENABLE_QA
static void qa_key(App *a, const char *key) {
    struct Binding {
        const char *name;
        SDL_Keycode code;
    } keys[] = {{"left", SDLK_LEFT},  {"right", SDLK_RIGHT},       {"up", SDLK_UP},
                {"down", SDLK_DOWN},  {"ok", SDLK_RETURN},         {"back", SDLK_ESCAPE},
                {"play", SDLK_SPACE}, {"nextpage", SDLK_PAGEDOWN}, {"prevpage", SDLK_PAGEUP},
                {"reload", SDLK_r}};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (!strcmp(key, keys[i].name)) {
            ui_key(a, keys[i].code, 0, false);
            return;
        }
}
#endif
int main(int argc, char **argv) {
#ifdef WEBOS
    native_log();
    struct sigaction crash = {0};
    crash.sa_sigaction = fatal_handler;
    crash.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &crash, NULL);
    sigaction(SIGABRT, &crash, NULL);
    sigaction(SIGBUS, &crash, NULL);
    sigaction(SIGILL, &crash, NULL);
#endif
    App app;
    memset(&app, 0, sizeof app);
    app.config.slideshow_seconds = 8;
    char error[ERROR_SIZE] = {0}, assets[1200] = {0}, base_dir[1024] = {0};
    bool hidden = false;
    const char *config_override = NULL;
    char *base = SDL_GetBasePath();
    copy_text(base_dir, sizeof base_dir, base ? base : "./");
    SDL_free(base);
    snprintf(assets, sizeof assets, "%sassets", base_dir);
    char font[1400];
    snprintf(font, sizeof font, "%s/Interface.ttf", assets);
    if (!exists(font))
        snprintf(assets, sizeof assets, "%s../assets", base_dir);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--hidden"))
            hidden = true;
        if (!strcmp(argv[i], "--config") && i + 1 < argc)
            config_override = argv[++i];
        if (!strcmp(argv[i], "--assets") && i + 1 < argc)
            copy_text(assets, sizeof assets, argv[++i]);
    }
    signal(SIGINT, interrupt_handler);
    signal(SIGTERM, interrupt_handler);
    signal(SIGPIPE, SIG_IGN);
    if (!api_init(error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    char ca[1400];
    snprintf(ca, sizeof ca, "%s/cacert.pem", assets);
    if (exists(ca))
        api_set_ca(ca);
    if (!ui_init(&app, assets, hidden, error)) {
        fprintf(stderr, "UI initialization failed: %s (%s)\n", error, SDL_GetError());
        return 1;
    }
    if (config_override)
        copy_text(app.config_path, sizeof app.config_path, config_override);
    else {
#ifdef WEBOS
        char state_dir[300];
        snprintf(state_dir, sizeof state_dir, "/media/developer/temp/%s", APP_ID);
        mkdir(state_dir, 0700);
        struct stat state_stat;
        if (lstat(state_dir, &state_stat) || !S_ISDIR(state_stat.st_mode) ||
            state_stat.st_uid != getuid() || (state_stat.st_mode & 0077)) {
            copy_text(app.notice, sizeof app.notice, "本机存储不可用，连接将仅保留到退出应用");
        } else
            snprintf(app.config_path, sizeof app.config_path, "%s/connection.json", state_dir);
#else
        char *pref = SDL_GetPrefPath("ImmichTV", APP_ID);
        snprintf(app.config_path, sizeof app.config_path, "%sconnection.json",
                 pref ? pref : base_dir);
        SDL_free(pref);
#endif
    }
    config_load(app.config_path, &app.config);
    cJSON *launch = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '{') {
            cJSON_Delete(launch);
            launch = cJSON_Parse(argv[i]);
            cJSON *parameters = cJSON_GetObjectItem(launch, "parameters");
            if (cJSON_IsObject(parameters)) {
                cJSON *nested = cJSON_Duplicate(parameters, true);
                cJSON_Delete(launch);
                launch = nested;
            }
            launch_config(launch, &app);
        } else if (!strcmp(argv[i], "--server") && i + 1 < argc)
            copy_text(app.config.server, sizeof app.config.server, argv[++i]);
        else if (!strcmp(argv[i], "--api-key") && i + 1 < argc)
            copy_text(app.config.api_key, sizeof app.config.api_key, argv[++i]);
        else if (!strcmp(argv[i], "--allow-http"))
            app.config.allow_http = true;
    }
    if (!worker_start()) {
        fprintf(stderr, "Worker initialization failed\n");
        return 1;
    }
    app.editing = app.config;
    if (*app.config.server && *app.config.api_key)
        app_load(&app, true);
    else
        app.settings = true;
    uint32_t beginning = SDL_GetTicks();
    bool foreground = true;
#ifdef ENABLE_QA
    cJSON *qa = cJSON_GetObjectItem(launch, "qa");
    int qa_index = 0;
    const char *capture = json_text(launch, "capturePath");
    uint32_t capture_after = (uint32_t)json_integer(launch, "captureAfterMs", 7000),
             exit_after = (uint32_t)json_integer(launch, "exitAfterMs", 0);
    bool captured = false;
#endif
    fprintf(stderr, "Immich TV %s ready; native=%d; read-only policy enabled\n", APP_VERSION,
#ifdef WEBOS
            1
#else
            0
#endif
    );
    while (!app.quit && !interrupted) {
        unsigned char raw[128];
        memset(raw, 0, sizeof raw);
        while (SDL_PollEvent((SDL_Event *)raw)) {
            uint32_t type = read_word(raw, 0);
            if (type == SDL_QUIT) {
                fprintf(stderr, "lifecycle: SDL_QUIT\n");
                app.quit = true;
            }
            if (type == SDL_KEYDOWN) {
#ifdef WEBOS
                unsigned state = read_word(raw, 16);
                if ((state & 0xff) == 1)
                    ui_key(&app, (SDL_Keycode)read_word(raw, 24), read_word(raw, 20),
                           (state & 0x100) != 0);
#else
                SDL_Event *event = (SDL_Event *)raw;
                ui_key(&app, event->key.keysym.sym, (unsigned)event->key.keysym.scancode,
                       event->key.repeat != 0);
#endif
            } else if (type == SDL_TEXTINPUT) {
#ifdef WEBOS
                char typed[33] = {0};
                memcpy(typed, raw + 16, 32);
                ui_text(&app, typed);
#else
                ui_text(&app, ((SDL_Event *)raw)->text.text);
#endif
            } else if (type == SDL_MOUSEMOTION || type == SDL_MOUSEBUTTONDOWN) {
                int x = (int)read_word(raw, 20), y = (int)read_word(raw, 24);
                if (type == SDL_MOUSEMOTION) {
                    x = (int)read_word(raw, 20);
                    y = (int)read_word(raw, 24);
                }
                /* SDL scales ordinary pointer events for the logical renderer. */
                ui_pointer(&app, x, y, type == SDL_MOUSEBUTTONDOWN);
            } else if (type == SDL_MOUSEWHEEL) {
                int dy = ((SDL_Event *)raw)->wheel.y;
                if (dy)
                    ui_key(&app, dy > 0 ? SDLK_UP : SDLK_DOWN, 0, false);
            }
#ifdef WEBOS
            if (type == 0x103 || type == 0x104) {
                foreground = false;
                if (app.video) {
                    media_stop();
                    app.video = false;
                }
                app.slideshow = false;
            }
            if (type == 0x106)
                foreground = true;
#endif
            memset(raw, 0, sizeof raw);
        }
        uint32_t now = SDL_GetTicks();
        app_tick(&app, now);
#ifdef ENABLE_QA
        uint32_t elapsed = now - beginning;
        while (cJSON_IsArray(qa) && qa_index < cJSON_GetArraySize(qa)) {
            cJSON *step = cJSON_GetArrayItem(qa, qa_index);
            if (elapsed < (uint32_t)json_integer(step, "at", 0))
                break;
            qa_key(&app, json_text(step, "key"));
            cJSON *pointer = cJSON_GetObjectItem(step, "pointer");
            if (cJSON_IsObject(pointer))
                ui_pointer(&app, json_integer(pointer, "x", 0), json_integer(pointer, "y", 0),
                           cJSON_IsTrue(cJSON_GetObjectItem(pointer, "click")));
            const char *shot = json_text(step, "capture");
            if (*shot) {
                ui_draw(&app, now);
                ui_capture(&app, shot);
            }
            ++qa_index;
        }
#endif
        if (foreground) {
            ui_draw(&app, now);
#ifdef ENABLE_QA
            if (!captured && *capture && elapsed >= capture_after) {
                if (ui_capture(&app, capture))
                    fprintf(stderr, "QA frame captured\n");
                captured = true;
            }
#endif
            SDL_RenderPresent(app.renderer);
        } else
            SDL_Delay(80);
#ifdef ENABLE_QA
        if (exit_after && elapsed >= exit_after)
            app.quit = true;
#endif
        SDL_Delay(8);
    }
    (void)beginning;
    fprintf(stderr, "application exit: requested=%d signal=%d\n", app.quit, interrupted);
    media_stop();
    worker_stop();
    ui_destroy(&app);
    api_cleanup();
    cJSON_Delete(launch);
    memset(&app.config, 0, sizeof app.config);
    return 0;
}
