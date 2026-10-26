#include "api.h"
#include "core.h"
#include "proxy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void policy_tests(void) {
    assert(read_request_allowed("GET", "/albums"));
    assert(read_request_allowed("GET", "/assets/123-ab/thumbnail?size=preview"));
    assert(read_request_allowed("POST", "/search/metadata"));
    assert(read_request_allowed("HEAD", "/assets/123-ab/video/playback"));
    const char *methods[] = {"DELETE", "PUT", "PATCH", "OPTIONS", "TRACE"};
    for (size_t i = 0; i < sizeof methods / sizeof methods[0]; i++)
        assert(!read_request_allowed(methods[i], "/assets/123"));
    const char *paths[] = {"/assets",
                           "/albums",
                           "/auth/login",
                           "/jobs",
                           "/assets/123/thumbnail/../../original",
                           "/assets/123%2f../original",
                           "//evil.test/assets",
                           "/assets/123/original",
                           "/assets/123/thumbnail\r\nX-Evil: yes"};
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++)
        assert(!read_request_allowed("POST", paths[i]));
    assert(!read_request_allowed("GET", "/assets/123/original"));
    assert(!read_request_allowed("GET", "/assets/a/../../albums"));
    assert(!read_request_allowed("GET", "/assets/a%2fthumbnail"));
    assert(!read_request_allowed("HEAD", "/albums"));
}
static void core_tests(void) {
    char out[768], error[ERROR_SIZE];
    assert(normalize_server("https://photos.example.com/photos/api/", out, sizeof out, error));
    assert(!strcmp(out, "https://photos.example.com/photos"));
    assert(!normalize_server("https://token@host", out, sizeof out, error));
    assert(!normalize_server("file:///etc/passwd", out, sizeof out, error));
    assert(!normalize_server("https://host?key=secret", out, sizeof out, error));
    assert(!normalize_server("https://host/../api", out, sizeof out, error));
    assert(!valid_asset_id("../auth"));
    assert(valid_asset_id("a-b-123"));
    assert(grid_move(4, 1, 0, 5, 12) == 4);
    assert(grid_move(5, -1, 0, 5, 12) == 5);
    assert(grid_move(9, 0, 1, 5, 12) == 11);
    assert(grid_move(11, 0, 1, 5, 12) == 11);
    assert(grid_move(0, 0, 1, 5, 0) == -1);
    FrameRect wide = fit_frame(1280, 720, 1920, 1080), portrait = fit_frame(1080, 1920, 1920, 1080);
    assert(wide.x == 0 && wide.y == 0 && wide.w == 1920 && wide.h == 1080);
    assert(portrait.h == 1080 && portrait.w == 607 && portrait.x == 656);
    cJSON *old = cJSON_CreateString("01:02:03.500"), *modern = cJSON_CreateNumber(12000);
    assert(parse_duration(old) == 3723.5);
    assert(parse_duration(modern) == 12.0);
    cJSON_Delete(old);
    cJSON_Delete(modern);
    cJSON *j = cJSON_Parse("{\"assets\":{\"items\":[{\"id\":\"safe\",\"type\":\"IMAGE\"},{\"id\":"
                           "\"hidden\",\"visibility\":\"hidden\"},{\"id\":\"trash\",\"isTrashed\":"
                           "true},{\"id\":\"locked\",\"visibility\":\"locked\"},{\"id\":\"../"
                           "bad\"}],\"nextCursor\":\"opaque-token\",\"nextPage\":null}}");
    Catalog *c = calloc(1, sizeof *c);
    assert(parse_assets(j, c, error));
    assert(c->count == 1);
    assert(!strcmp(c->assets[0].id, "safe"));
    assert(!strcmp(c->next_cursor, "opaque-token"));
    catalog_free(c);
    cJSON_Delete(j);
    char directory[] = "/tmp/immich-tv-unit-XXXXXX";
    assert(mkdtemp(directory));
    char path[1024];
    snprintf(path, sizeof path, "%s/config.json", directory);
    Config original = {.allow_http = true, .remember = true, .slideshow_seconds = 8}, loaded = {0};
    assert(!config_save("", &original, error));
    copy_text(original.server, sizeof original.server, "http://127.0.0.1:22830");
    copy_text(original.api_key, sizeof original.api_key, "unit-test-secret");
    assert(config_save(path, &original, error));
    struct stat st;
    assert(!stat(path, &st));
    assert((st.st_mode & 0777) == 0600);
    assert(config_load(path, &loaded));
    assert(!strcmp(loaded.api_key, original.api_key));
    chmod(path, 0644);
    assert(!config_load(path, &loaded));
    unlink(path);
    assert(symlink("/etc/passwd", path) == 0);
    assert(!config_load(path, &loaded));
    unlink(path);
    rmdir(directory);
}
static int integration(const char *server, const char *key, const char *mode) {
    Config c = {.allow_http = true, .slideshow_seconds = 8};
    copy_text(c.server, sizeof c.server, server);
    copy_text(c.api_key, sizeof c.api_key, key);
    char error[ERROR_SIZE] = {0};
    assert(api_init(error));
    Catalog *assets = api_catalog(&c, VIEW_TIMELINE, "", 1, "", error);
    if (!strcmp(mode, "unauthorized")) {
        assert(!assets);
        assert(strstr(error, "密钥"));
        api_cleanup();
        return 0;
    }
    assert(assets && assets->count == 40);
    assert(assets->next_page || *assets->next_cursor);
    Catalog *next = api_catalog(&c, VIEW_TIMELINE, "", assets->next_page ? assets->next_page : 2,
                                assets->next_cursor, error);
    assert(next && next->count == 7);
    assert(strcmp(assets->assets[0].id, next->assets[0].id));
    Catalog *favorite = api_catalog(&c, VIEW_FAVORITES, "", 1, "", error);
    assert(favorite && favorite->count == 12);
    for (size_t i = 0; i < favorite->count; i++)
        assert(favorite->assets[i].favorite);
    Catalog *videos = api_catalog(&c, VIEW_VIDEOS, "", 1, "", error);
    assert(videos && videos->count == 3);
    assert(videos->assets[0].duration == 12.0);
    Catalog *albums = api_catalog(&c, VIEW_ALBUMS, "", 1, "", error);
    assert(albums && albums->count == 6);
    Catalog *album = api_catalog(&c, VIEW_ALBUMS, albums->albums[0].id, 1, "", error);
    assert(album && album->count > 0 && album->count < 40);
    Catalog *empty = api_catalog(&c, VIEW_ALBUMS, albums->albums[5].id, 1, "", error);
    assert(empty && empty->count == 0);
    if (!strcmp(mode, "proxy")) {
        char url[256];
        assert(proxy_start(&c, videos->assets[0].id, url, sizeof url, error));
        printf("%s\n", url);
        fflush(stdout);
        sleep(8);
        proxy_stop();
    }
    catalog_free(assets);
    catalog_free(next);
    catalog_free(favorite);
    catalog_free(videos);
    catalog_free(albums);
    catalog_free(album);
    catalog_free(empty);
    api_cleanup();
    return 0;
}
int main(int argc, char **argv) {
    if (argc == 4)
        return integration(argv[1], argv[2], argv[3]);
    policy_tests();
    core_tests();
    puts("core: read-only policy, pagination focus, compatibility parsing, credential storage "
         "passed");
    return 0;
}
