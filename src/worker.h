#ifndef IMMICH_TV_WORKER_H
#define IMMICH_TV_WORKER_H
#include "api.h"
#include <SDL.h>
typedef enum { JOB_CATALOG, JOB_IMAGE } JobKind;
typedef struct {
    JobKind kind;
    unsigned generation;
    Config config;
    View view;
    char album[80], cursor[1024], id[80];
    int page;
    bool preview;
} Job;
typedef struct Result {
    Job job;
    Catalog *catalog;
    SDL_Surface *surface;
    char error[ERROR_SIZE];
    struct Result *next;
} Result;
bool worker_start(void);
bool worker_submit(const Job *job, bool priority);
Result *worker_poll(void);
void worker_cancel_pending(void);
void result_free(Result *r);
void worker_stop(void);
#endif
