#ifndef IMMICH_TV_DEMUX_H
#define IMMICH_TV_DEMUX_H
#include "core.h"
typedef struct Demux Demux;
typedef struct {
    char video_codec[16], audio_codec[16];
    int width, height, fps;
    double duration;
} DemuxInfo;
typedef struct {
    unsigned char *data;
    size_t size;
    int64_t pts_ns;
    bool audio;
} MediaPacket;
Demux *demux_start(const Config *config, const char *asset);
bool demux_info(Demux *demux, DemuxInfo *info);
bool demux_packet(Demux *demux, MediaPacket *packet);
void demux_packet_free(MediaPacket *packet);
void demux_seek(Demux *demux, double seconds);
bool demux_finished(Demux *demux);
bool demux_error(Demux *demux, char *error);
void demux_stop(Demux *demux);
#endif
