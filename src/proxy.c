/* Authenticated, range-preserving video transport. The key stays in HTTP headers
 * upstream, never in a player URL. Only one selected asset is exposed, only on
 * loopback, with a fresh unguessable path for each playback. */
#include "proxy.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static struct {
    Config config;
    char asset[80], path[80];
    int listener, client;
    volatile int stop;
    pthread_t thread;
    bool active;
} proxy;
typedef struct {
    int socket;
    bool sent;
    int status;
    char headers[2048];
    size_t len;
} Relay;
static bool send_all(int fd, const char *p, size_t n) {
    while (n) {
#ifdef MSG_NOSIGNAL
        ssize_t s = send(fd, p, n, MSG_NOSIGNAL);
#else
        ssize_t s = send(fd, p, n, 0);
#endif
        if (s <= 0)
            return false;
        p += s;
        n -= (size_t)s;
    }
    return true;
}
static size_t relay_header(char *data, size_t z, size_t n, void *ctx) {
    Relay *r = ctx;
    size_t len = z * n;
    if (len > 4096)
        return 0;
    char line[4097];
    memcpy(line, data, len);
    line[len] = 0;
    if (!strncmp(line, "HTTP/", 5)) {
        char *space = strchr(line, ' ');
        r->status = space ? atoi(space + 1) : 502;
        r->len = 0;
    } else if (!strcmp(line, "\r\n") && r->status >= 200) {
        char status[160];
        int s = snprintf(status, sizeof status, "HTTP/1.0 %d %s\r\nConnection: close\r\n",
                         r->status, r->status == 206 ? "Partial Content" : "OK");
        if (!send_all(r->socket, status, (size_t)s) || !send_all(r->socket, r->headers, r->len) ||
            !send_all(r->socket, "\r\n", 2))
            return 0;
        r->sent = true;
    } else if (!strncasecmp(line, "Content-Type:", 13) ||
               !strncasecmp(line, "Content-Length:", 15) ||
               !strncasecmp(line, "Content-Range:", 14) ||
               !strncasecmp(line, "Accept-Ranges:", 14)) {
        if (r->len + len >= sizeof r->headers)
            return 0;
        memcpy(r->headers + r->len, line, len);
        r->len += len;
    }
    return len;
}
static size_t relay_body(char *data, size_t z, size_t n, void *ctx) {
    Relay *r = ctx;
    size_t len = z * n;
    return !__atomic_load_n(&proxy.stop, __ATOMIC_ACQUIRE) && r->sent &&
                   send_all(r->socket, data, len)
               ? len
               : 0;
}
static int relay_cancel(void *unused) {
    (void)unused;
    return __atomic_load_n(&proxy.stop, __ATOMIC_ACQUIRE);
}
static void *serve(void *unused) {
    (void)unused;
    while (!__atomic_load_n(&proxy.stop, __ATOMIC_ACQUIRE)) {
        int fd = accept(proxy.listener, NULL, NULL);
        if (fd < 0)
            break;
        __atomic_store_n(&proxy.client, fd, __ATOMIC_RELEASE);
        struct timeval timeout = {3, 0};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
#ifdef SO_NOSIGPIPE
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#endif
        char request[4096] = {0};
        size_t used = 0;
        while (used < sizeof request - 1 && !strstr(request, "\r\n\r\n")) {
            ssize_t n = recv(fd, request + used, sizeof request - used - 1, 0);
            if (n <= 0)
                break;
            used += (size_t)n;
            request[used] = 0;
        }
        char method[8], path[128], range[96] = {0};
        bool valid = sscanf(request, "%7s %127s", method, path) == 2 &&
                     (!strcmp(method, "GET") || !strcmp(method, "HEAD")) &&
                     !strcmp(path, proxy.path);
        char *line = request;
        while ((line = strstr(line, "\r\n"))) {
            line += 2;
            if (!strncasecmp(line, "Range: bytes=", 13)) {
                size_t n = strcspn(line + 13, "\r\n");
                if (n >= sizeof range) {
                    valid = false;
                    break;
                }
                memcpy(range, line + 13, n);
                for (size_t i = 0; i < n; ++i)
                    if ((range[i] < '0' || range[i] > '9') && range[i] != '-')
                        valid = false;
            }
        }
        if (valid) {
            Relay relay = {.socket = fd};
            char error[ERROR_SIZE] = {0};
            api_stream(&proxy.config, proxy.asset, range, !strcmp(method, "HEAD"), relay_header,
                       relay_body, &relay, relay_cancel, error);
            if (!relay.sent) {
                const char *response = "HTTP/1.0 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n";
                send_all(fd, response, strlen(response));
            }
        } else {
            const char *response = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send_all(fd, response, strlen(response));
        }
        shutdown(fd, SHUT_RDWR);
        close(fd);
        __atomic_store_n(&proxy.client, -1, __ATOMIC_RELEASE);
    }
    return NULL;
}
bool proxy_start(const Config *config, const char *asset, char *url, size_t cap, char *error) {
    proxy_stop();
    memset(&proxy, 0, sizeof proxy);
    proxy.listener = proxy.client = -1;
    if (!valid_asset_id(asset))
        return false;
    unsigned char random[16];
    int fd = open("/dev/urandom", O_RDONLY);
    ssize_t n = fd >= 0 ? read(fd, random, sizeof random) : -1;
    if (fd >= 0)
        close(fd);
    if (n != sizeof random)
        return false;
    copy_text(proxy.path, sizeof proxy.path, "/stream/");
    for (int i = 0; i < 16; ++i)
        snprintf(proxy.path + 8 + i * 2, 3, "%02x", random[i]);
    proxy.config = *config;
    copy_text(proxy.asset, sizeof proxy.asset, asset);
    proxy.listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (proxy.listener < 0 || bind(proxy.listener, (struct sockaddr *)&address, sizeof address) ||
        listen(proxy.listener, 4))
        goto fail;
    socklen_t length = sizeof address;
    if (getsockname(proxy.listener, (struct sockaddr *)&address, &length))
        goto fail;
    snprintf(url, cap, "http://127.0.0.1:%u%s", ntohs(address.sin_port), proxy.path);
    if (pthread_create(&proxy.thread, NULL, serve, NULL))
        goto fail;
    proxy.active = true;
    return true;
fail:
    if (proxy.listener >= 0)
        close(proxy.listener);
    copy_text(error, ERROR_SIZE, "无法创建本机视频传输通道");
    memset(&proxy.config, 0, sizeof proxy.config);
    return false;
}
void proxy_stop(void) {
    if (!proxy.active)
        return;
    __atomic_store_n(&proxy.stop, 1, __ATOMIC_RELEASE);
    shutdown(proxy.listener, SHUT_RDWR);
    close(proxy.listener);
    int client = __atomic_load_n(&proxy.client, __ATOMIC_ACQUIRE);
    if (client >= 0)
        shutdown(client, SHUT_RDWR);
    pthread_join(proxy.thread, NULL);
    memset(&proxy.config, 0, sizeof proxy.config);
    proxy.active = false;
}
