#include "worker.h"
#include <SDL_image.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define QUEUE_CAP 96
static struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    Job jobs[QUEUE_CAP];
    size_t count;
    Result *head, *tail;
    bool stop, started;
} worker;

static void *run(void *unused) {
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&worker.mutex);
        while (!worker.count && !worker.stop)
            pthread_cond_wait(&worker.ready, &worker.mutex);
        if (worker.stop) {
            pthread_mutex_unlock(&worker.mutex);
            break;
        }
        Job j = worker.jobs[0];
        memmove(worker.jobs, worker.jobs + 1, (--worker.count) * sizeof(Job));
        pthread_mutex_unlock(&worker.mutex);
        Result *r = calloc(1, sizeof *r);
        if (!r)
            continue;
        r->job = j;
        if (j.kind == JOB_CATALOG)
            r->catalog = api_catalog(&j.config, j.view, j.album, j.page, j.cursor, r->error);
        else {
            size_t len = 0;
            unsigned char *bytes = api_image(&j.config, j.id, j.preview, &len, r->error);
            if (bytes && len <= INT32_MAX) {
                SDL_RWops *rw = SDL_RWFromConstMem(bytes, (int)len);
                r->surface = rw ? IMG_Load_RW(rw, 1) : NULL;
            }
            free(bytes);
            /* Some LG SDL_image builds omit WebP. Immich preview images provide
               a JPEG compatibility route, still resized to a thumbnail here. */
            if (!r->surface && !j.preview) {
                bytes = api_image(&j.config, j.id, true, &len, r->error);
                if (bytes && len <= INT32_MAX) {
                    SDL_RWops *rw = SDL_RWFromConstMem(bytes, (int)len);
                    r->surface = rw ? IMG_Load_RW(rw, 1) : NULL;
                }
                free(bytes);
            }
            {
                if (r->surface) {
                    int maxw = j.preview ? 1920 : 512, maxh = j.preview ? 1080 : 512;
                    double scale = (double)maxw / r->surface->w;
                    if ((double)maxh / r->surface->h < scale)
                        scale = (double)maxh / r->surface->h;
                    if (scale < 1.0) {
                        SDL_Surface *small = SDL_CreateRGBSurfaceWithFormat(
                            0, (int)(r->surface->w * scale), (int)(r->surface->h * scale), 32,
                            SDL_PIXELFORMAT_RGBA32);
                        if (small) {
                            SDL_BlitScaled(r->surface, NULL, small, NULL);
                            SDL_FreeSurface(r->surface);
                            r->surface = small;
                        }
                    }
                }
                if (!r->surface)
                    copy_text(r->error, sizeof r->error,
                              "无法解码这张照片，请检查服务端缩略图任务");
            }
        }
        pthread_mutex_lock(&worker.mutex);
        if (worker.tail)
            worker.tail->next = r;
        else
            worker.head = r;
        worker.tail = r;
        pthread_mutex_unlock(&worker.mutex);
    }
    return NULL;
}
bool worker_start(void) {
    memset(&worker, 0, sizeof worker);
    if (pthread_mutex_init(&worker.mutex, NULL) || pthread_cond_init(&worker.ready, NULL))
        return false;
    worker.started = pthread_create(&worker.thread, NULL, run, NULL) == 0;
    return worker.started;
}
bool worker_submit(const Job *job, bool priority) {
    bool ok = false;
    pthread_mutex_lock(&worker.mutex);
    if (worker.count < QUEUE_CAP && !worker.stop) {
        size_t at = priority ? 0 : worker.count;
        if (priority)
            memmove(worker.jobs + 1, worker.jobs, worker.count * sizeof(Job));
        worker.jobs[at] = *job;
        ++worker.count;
        ok = true;
        pthread_cond_signal(&worker.ready);
    }
    pthread_mutex_unlock(&worker.mutex);
    return ok;
}
Result *worker_poll(void) {
    pthread_mutex_lock(&worker.mutex);
    Result *r = worker.head;
    if (r) {
        worker.head = r->next;
        if (!worker.head)
            worker.tail = NULL;
        r->next = NULL;
    }
    pthread_mutex_unlock(&worker.mutex);
    return r;
}
void worker_cancel_pending(void) {
    pthread_mutex_lock(&worker.mutex);
    memset(worker.jobs, 0, sizeof worker.jobs);
    worker.count = 0;
    pthread_mutex_unlock(&worker.mutex);
}
void result_free(Result *r) {
    if (r) {
        catalog_free(r->catalog);
        if (r->surface)
            SDL_FreeSurface(r->surface);
        memset(&r->job.config, 0, sizeof(Config));
        free(r);
    }
}
void worker_stop(void) {
    if (!worker.started)
        return;
    pthread_mutex_lock(&worker.mutex);
    worker.stop = true;
    worker.count = 0;
    pthread_cond_signal(&worker.ready);
    pthread_mutex_unlock(&worker.mutex);
    pthread_join(worker.thread, NULL);
    Result *r;
    while ((r = worker_poll()))
        result_free(r);
    pthread_mutex_destroy(&worker.mutex);
    pthread_cond_destroy(&worker.ready);
    memset(&worker, 0, sizeof worker);
}
