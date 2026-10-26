#ifndef IMMICH_TV_PROXY_H
#define IMMICH_TV_PROXY_H
#include "api.h"
bool proxy_start(const Config *config, const char *asset, char *url, size_t cap, char *error);
void proxy_stop(void);
#endif
