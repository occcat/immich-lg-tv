#include "demux.h"
#include "api.h"
#include <errno.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACKET_CAP 128
#define PACKET_BYTES (16 * 1024 * 1024)
#define CHUNK_BYTES (1024 * 1024)
struct Demux {
    Config config;
    char asset[80], error[ERROR_SIZE];
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t space;
    bool stop, ready, finished;
    DemuxInfo info;
    MediaPacket packets[PACKET_CAP];
    size_t count, bytes;
    int64_t seek_us;
    unsigned char *chunk;
    size_t chunk_size;
    int64_t chunk_start, offset, total;
};
static void set_failure(Demux *d, const char *message);
static int cancelled(void *opaque) {
    Demux *d = opaque;
    pthread_mutex_lock(&d->mutex);
    bool stop = d->stop;
    pthread_mutex_unlock(&d->mutex);
    return stop;
}
static int read_packet(void *opaque, uint8_t *data, int wanted) {
    Demux *d = opaque;
    if (cancelled(d))
        return AVERROR_EXIT;
    if (d->total > 0 && d->offset >= d->total)
        return AVERROR_EOF;
    if (!d->chunk || d->offset < d->chunk_start ||
        d->offset >= d->chunk_start + (int64_t)d->chunk_size) {
        free(d->chunk);
        d->chunk = NULL;
        d->chunk_start = d->offset;
        char error[ERROR_SIZE] = {0};
        d->chunk = api_video_chunk(&d->config, d->asset, d->offset, CHUNK_BYTES, &d->chunk_size,
                                   &d->total, d, cancelled, error);
        if (!d->chunk) {
            if (!cancelled(d))
                set_failure(d, error);
            return AVERROR(EIO);
        }
        if (!d->chunk_size)
            return AVERROR_EOF;
    }
    size_t at = (size_t)(d->offset - d->chunk_start), n = d->chunk_size - at;
    if (n > (size_t)wanted)
        n = (size_t)wanted;
    memcpy(data, d->chunk + at, n);
    d->offset += (int64_t)n;
    return (int)n;
}
static int64_t seek_file(void *opaque, int64_t offset, int whence) {
    Demux *d = opaque;
    if (whence == AVSEEK_SIZE)
        return d->total > 0 ? d->total : AVERROR(ENOSYS);
    whence &= ~AVSEEK_FORCE;
    int64_t target = whence == SEEK_SET                   ? offset
                     : whence == SEEK_CUR                 ? d->offset + offset
                     : whence == SEEK_END && d->total > 0 ? d->total + offset
                                                          : -1;
    if (target < 0 || (d->total > 0 && target > d->total))
        return AVERROR(EINVAL);
    d->offset = target;
    return target;
}
void demux_packet_free(MediaPacket *p) {
    if (p) {
        free(p->data);
        memset(p, 0, sizeof *p);
    }
}
static bool queue_packet(Demux *d, const uint8_t *data, int size, int64_t pts, bool audio,
                         const AVCodecParameters *codec) {
    if (size <= 0 || size > 8 * 1024 * 1024)
        return false;
    bool adts = audio && codec->codec_id == AV_CODEC_ID_AAC;
    size_t n = (size_t)size + (adts ? 7 : 0);
    unsigned char *copy = malloc(n);
    if (!copy)
        return false;
    if (adts) {
        static const int rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                    22050, 16000, 12000, 11025, 8000,  7350};
        int rate = 3;
        for (int i = 0; i < 13; i++)
            if (rates[i] == codec->sample_rate)
                rate = i;
        int channels = codec->ch_layout.nb_channels, profile = 1;
        if (codec->extradata_size >= 2) {
            int object = codec->extradata[0] >> 3;
            if (object >= 1 && object <= 4)
                profile = object - 1;
        }
        copy[0] = 0xff;
        copy[1] = 0xf1;
        copy[2] = (unsigned char)((profile << 6) | (rate << 2) | (channels >> 2));
        copy[3] = (unsigned char)(((channels & 3) << 6) | (n >> 11));
        copy[4] = (unsigned char)(n >> 3);
        copy[5] = (unsigned char)(((n & 7) << 5) | 0x1f);
        copy[6] = 0xfc;
    }
    memcpy(copy + (adts ? 7 : 0), data, (size_t)size);
    pthread_mutex_lock(&d->mutex);
    while (!d->stop && d->seek_us < 0 && (d->count >= PACKET_CAP || d->bytes + n > PACKET_BYTES))
        pthread_cond_wait(&d->space, &d->mutex);
    bool ok = !d->stop && d->seek_us < 0;
    if (ok) {
        d->packets[d->count++] = (MediaPacket){copy, n, pts, audio};
        d->bytes += n;
    } else
        free(copy);
    pthread_mutex_unlock(&d->mutex);
    return ok;
}
static void set_failure(Demux *d, const char *message) {
    pthread_mutex_lock(&d->mutex);
    if (!*d->error)
        copy_text(d->error, sizeof d->error, message);
    pthread_mutex_unlock(&d->mutex);
}
static void *decode(void *opaque) {
    Demux *d = opaque;
    AVFormatContext *format = avformat_alloc_context();
    unsigned char *io_buffer = av_malloc(32768);
    AVIOContext *io =
        io_buffer ? avio_alloc_context(io_buffer, 32768, 0, d, read_packet, NULL, seek_file) : NULL;
    AVPacket *packet = av_packet_alloc(), *filtered = av_packet_alloc();
    AVBSFContext *bsf = NULL;
    if (!format || !io || !packet || !filtered) {
        set_failure(d, "视频解析器内存不足");
        goto done;
    }
    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    format->interrupt_callback.callback = cancelled;
    format->interrupt_callback.opaque = d;
    format->probesize = 2 * 1024 * 1024;
    format->max_analyze_duration = 2 * AV_TIME_BASE;
    if (avformat_open_input(&format, NULL, NULL, NULL) < 0 ||
        avformat_find_stream_info(format, NULL) < 0) {
        set_failure(d, "无法读取视频容器，请检查服务端转码文件");
        goto done;
    }
    int video = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    int audio = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (video < 0) {
        set_failure(d, "文件中没有可播放的视频轨道");
        goto done;
    }
    AVCodecParameters *v = format->streams[video]->codecpar;
    const char *filter = v->codec_id == AV_CODEC_ID_H264   ? "h264_mp4toannexb"
                         : v->codec_id == AV_CODEC_ID_HEVC ? "hevc_mp4toannexb"
                                                           : NULL;
    if (!filter) {
        set_failure(d, "当前版本支持 H.264 / HEVC，请在 Immich 中转码后播放");
        goto done;
    }
    copy_text(d->info.video_codec, sizeof d->info.video_codec,
              v->codec_id == AV_CODEC_ID_H264 ? "H264" : "H265");
    if (audio >= 0) {
        AVCodecParameters *a = format->streams[audio]->codecpar;
        const char *name = a->codec_id == AV_CODEC_ID_AAC    ? "AAC"
                           : a->codec_id == AV_CODEC_ID_AC3  ? "AC3"
                           : a->codec_id == AV_CODEC_ID_EAC3 ? "AC3 PLUS"
                                                             : NULL;
        if (!name) {
            set_failure(d, "当前音轨不受支持，请在 Immich 中转码为 AAC");
            goto done;
        }
        copy_text(d->info.audio_codec, sizeof d->info.audio_codec, name);
    }
    const AVBitStreamFilter *f = av_bsf_get_by_name(filter);
    if (!f || av_bsf_alloc(f, &bsf) < 0 || avcodec_parameters_copy(bsf->par_in, v) < 0) {
        set_failure(d, "视频格式转换器不可用");
        goto done;
    }
    bsf->time_base_in = format->streams[video]->time_base;
    if (av_bsf_init(bsf) < 0) {
        set_failure(d, "无法读取视频编码信息");
        goto done;
    }
    d->info.width = v->width;
    d->info.height = v->height;
    AVRational fps = av_guess_frame_rate(format, format->streams[video], NULL);
    d->info.fps = fps.den > 0 ? (int)(av_q2d(fps) + .5) : 30;
    if (d->info.fps <= 0)
        d->info.fps = 30;
    d->info.duration = format->duration > 0 ? format->duration / (double)AV_TIME_BASE : 0;
    pthread_mutex_lock(&d->mutex);
    d->ready = true;
    pthread_mutex_unlock(&d->mutex);
    int64_t origin = format->start_time != AV_NOPTS_VALUE ? format->start_time : 0;
    while (!cancelled(d)) {
        pthread_mutex_lock(&d->mutex);
        int64_t seek = d->seek_us;
        d->seek_us = -1;
        pthread_mutex_unlock(&d->mutex);
        if (seek >= 0) {
            av_seek_frame(format, -1, seek + origin, AVSEEK_FLAG_BACKWARD);
            av_bsf_flush(bsf);
        }
        int read = av_read_frame(format, packet);
        if (read < 0) {
            if (read != AVERROR_EOF && !cancelled(d))
                set_failure(d, "视频传输中断，请返回后重试");
            pthread_mutex_lock(&d->mutex);
            d->finished = true;
            /* An EOF does not retire the demuxer: the viewer may seek back while
               the decoder is still presenting its buffered frames. */
            while (read == AVERROR_EOF && !d->stop && d->seek_us < 0)
                pthread_cond_wait(&d->space, &d->mutex);
            bool retry = read == AVERROR_EOF && !d->stop;
            if (retry)
                d->finished = false;
            pthread_mutex_unlock(&d->mutex);
            if (retry)
                continue;
            break;
        }
        int stream = packet->stream_index;
        if (stream == video) {
            if (av_bsf_send_packet(bsf, packet) >= 0) {
                while (av_bsf_receive_packet(bsf, filtered) >= 0) {
                    int64_t pts = filtered->pts != AV_NOPTS_VALUE ? filtered->pts : filtered->dts;
                    if (pts == AV_NOPTS_VALUE)
                        pts = 0;
                    pts = av_rescale_q(pts, bsf->time_base_out, (AVRational){1, 1000000000}) -
                          origin * 1000;
                    if (pts < 0)
                        pts = 0;
                    queue_packet(d, filtered->data, filtered->size, pts, false, v);
                    av_packet_unref(filtered);
                }
            }
        } else if (stream == audio) {
            int64_t pts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
            if (pts == AV_NOPTS_VALUE)
                pts = 0;
            pts =
                av_rescale_q(pts, format->streams[audio]->time_base, (AVRational){1, 1000000000}) -
                origin * 1000;
            if (pts >= 0)
                queue_packet(d, packet->data, packet->size, pts, true,
                             format->streams[audio]->codecpar);
        }
        av_packet_unref(packet);
    }
done:
    av_bsf_free(&bsf);
    av_packet_free(&packet);
    av_packet_free(&filtered);
    if (format)
        avformat_close_input(&format);
    if (io) {
        av_freep(&io->buffer);
        avio_context_free(&io);
    } else
        av_free(io_buffer);
    return NULL;
}
Demux *demux_start(const Config *config, const char *asset) {
    Demux *d = calloc(1, sizeof *d);
    if (!d)
        return NULL;
    d->config = *config;
    copy_text(d->asset, sizeof d->asset, asset);
    d->seek_us = -1;
    pthread_mutex_init(&d->mutex, NULL);
    pthread_cond_init(&d->space, NULL);
    if (pthread_create(&d->thread, NULL, decode, d)) {
        pthread_mutex_destroy(&d->mutex);
        pthread_cond_destroy(&d->space);
        free(d);
        return NULL;
    }
    return d;
}
bool demux_info(Demux *d, DemuxInfo *info) {
    if (!d)
        return false;
    pthread_mutex_lock(&d->mutex);
    bool ready = d->ready;
    if (ready)
        *info = d->info;
    pthread_mutex_unlock(&d->mutex);
    return ready;
}
bool demux_packet(Demux *d, MediaPacket *packet) {
    if (!d)
        return false;
    pthread_mutex_lock(&d->mutex);
    bool ok = d->count > 0;
    if (ok) {
        *packet = d->packets[0];
        d->bytes -= packet->size;
        memmove(d->packets, d->packets + 1, (--d->count) * sizeof(MediaPacket));
        pthread_cond_signal(&d->space);
    }
    pthread_mutex_unlock(&d->mutex);
    return ok;
}
void demux_seek(Demux *d, double seconds) {
    if (!d)
        return;
    pthread_mutex_lock(&d->mutex);
    for (size_t i = 0; i < d->count; i++)
        demux_packet_free(&d->packets[i]);
    d->count = d->bytes = 0;
    d->seek_us = (int64_t)(seconds * AV_TIME_BASE);
    d->finished = false;
    pthread_cond_signal(&d->space);
    pthread_mutex_unlock(&d->mutex);
}
bool demux_finished(Demux *d) {
    if (!d)
        return false;
    pthread_mutex_lock(&d->mutex);
    bool done = d->finished && d->count == 0;
    pthread_mutex_unlock(&d->mutex);
    return done;
}
bool demux_error(Demux *d, char *error) {
    if (!d)
        return false;
    pthread_mutex_lock(&d->mutex);
    bool failed = *d->error != 0;
    if (failed)
        copy_text(error, ERROR_SIZE, d->error);
    pthread_mutex_unlock(&d->mutex);
    return failed;
}
void demux_stop(Demux *d) {
    if (!d)
        return;
    pthread_mutex_lock(&d->mutex);
    d->stop = true;
    pthread_cond_signal(&d->space);
    pthread_mutex_unlock(&d->mutex);
    pthread_join(d->thread, NULL);
    for (size_t i = 0; i < d->count; i++)
        demux_packet_free(&d->packets[i]);
    free(d->chunk);
    pthread_mutex_destroy(&d->mutex);
    pthread_cond_destroy(&d->space);
    memset(&d->config, 0, sizeof d->config);
    free(d);
}
