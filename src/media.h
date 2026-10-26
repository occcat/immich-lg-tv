#ifndef IMMICH_TV_MEDIA_H
#define IMMICH_TV_MEDIA_H
#include "core.h"
bool media_start(const Config *config, const Asset *asset, char *error);
void media_stop(void);
void media_shutdown(void);
void media_pause(bool paused);
void media_seek(double seconds);
/* 0 loading, 1 playing, 2 ended, -1 failed. */
int media_state(void);
double media_position(void);
const char *media_error(void);
#endif
