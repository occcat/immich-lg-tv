#ifdef WEBOS
#include "demux.h"
#include "media.h"
#include "starfish.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE *elogf = NULL;
static Demux *demux;
static DemuxInfo info;
static MediaPacket pending;
static volatile unsigned epoch, rendered;
static volatile int state;
static bool loaded, attempted, eos, active, started, held;
static unsigned fed;
static uint32_t clock_start;
static double clock_base;
static char failure[ERROR_SIZE];

void sf_on_event(unsigned e, int type, long long num, const char *str) {
    (void)str;
    if (e != __atomic_load_n(&epoch, __ATOMIC_ACQUIRE))
        return;
    if (type == 22 || type == 26)
        __atomic_store_n(&state, 1, __ATOMIC_RELEASE);
    if (type == 18 || type == 19)
        __atomic_store_n(&state, -1, __ATOMIC_RELEASE);
    if (type == 28)
        __atomic_store_n(&state, 2, __ATOMIC_RELEASE);
    if (type == 49 && num > 0)
        __atomic_add_fetch(&rendered, (unsigned)num, __ATOMIC_ACQ_REL);
    /* Callback strings may contain a private media URL; record only numeric codes. */
    if (elogf &&
        (type == 22 || type == 18 || type == 23 || type == 26 || type == 27 || type == 28)) {
        fprintf(elogf, "media event=%d code=%lld\n", type, num);
        fflush(elogf);
    }
}
void acb_on_event(long event, const char *reply) {
    (void)event;
    (void)reply;
}

static bool load_native(void) {
    const char *window = vp_create_window();
    if (!window || vp_mode() != VP_EXPORTED) {
        copy_text(failure, sizeof failure, "视频功能需要 webOS 5 或更新版本");
        return false;
    }
    cJSON *root = cJSON_CreateObject(), *arg = cJSON_CreateObject();
    cJSON_AddItemToArray(cJSON_AddArrayToObject(root, "args"), arg);
    cJSON_AddStringToObject(arg, "mediaTransportType", "BUFFERSTREAM");
    cJSON *option = cJSON_AddObjectToObject(arg, "option");
    cJSON_AddStringToObject(option, "appId", APP_ID);
    cJSON_AddStringToObject(option, "windowId", window);
    cJSON_AddBoolToObject(option, "needAudio", *info.audio_codec != 0);
    cJSON_AddBoolToObject(option, "queryPosition", false);
    cJSON_AddBoolToObject(option, "lowDelayMode", false);
    cJSON *external = cJSON_AddObjectToObject(option, "externalStreamingInfo");
    cJSON *contents = cJSON_AddObjectToObject(external, "contents"),
          *codec = cJSON_AddObjectToObject(contents, "codec");
    cJSON_AddStringToObject(codec, "video", info.video_codec);
    if (*info.audio_codec)
        cJSON_AddStringToObject(codec, "audio", info.audio_codec);
    cJSON_AddStringToObject(contents, "format", "RAW");
    cJSON_AddStringToObject(contents, "provider", "immichtv");
    cJSON *es = cJSON_AddObjectToObject(contents, "esInfo");
    cJSON_AddBoolToObject(es, "pauseAtDecodeTime", false);
    cJSON_AddNumberToObject(es, "ptsToDecode", 0);
    cJSON_AddBoolToObject(es, "seperatedPTS", true);
    cJSON_AddBoolToObject(external, "streamQualityInfo", true);
    cJSON_AddBoolToObject(external, "streamQualityInfoNonFlushable", true);
    cJSON_AddBoolToObject(external, "audioSync", true);
    cJSON_AddBoolToObject(external, "restartStreaming", false);
    cJSON *buffer = cJSON_AddObjectToObject(external, "bufferingCtrInfo");
    cJSON_AddNumberToObject(buffer, "bufferMaxLevel", 0);
    cJSON_AddNumberToObject(buffer, "bufferMinLevel", 0);
    cJSON_AddNumberToObject(buffer, "preBufferByte", 0);
    cJSON_AddNumberToObject(buffer, "qBufferLevelAudio", 0);
    cJSON_AddNumberToObject(buffer, "qBufferLevelVideo", 0);
    cJSON *audio = cJSON_AddObjectToObject(buffer, "srcBufferLevelAudio"),
          *video = cJSON_AddObjectToObject(buffer, "srcBufferLevelVideo");
    cJSON_AddNumberToObject(audio, "minimum", 1);
    cJSON_AddNumberToObject(audio, "maximum", 1048576);
    cJSON_AddNumberToObject(video, "minimum", 1);
    cJSON_AddNumberToObject(video, "maximum", 8388608);
    cJSON_AddStringToObject(cJSON_AddObjectToObject(option, "transmission"), "contentsType",
                            "LIVE");
    cJSON *adaptive = cJSON_AddObjectToObject(option, "adaptiveStreaming");
    cJSON_AddBoolToObject(adaptive, "audioOnly", false);
    cJSON_AddNumberToObject(adaptive, "maxWidth", info.width);
    cJSON_AddNumberToObject(adaptive, "maxHeight", info.height);
    cJSON_AddNumberToObject(adaptive, "maxFrameRate", info.fps);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    unsigned next = __atomic_add_fetch(&epoch, 1, __ATOMIC_ACQ_REL);
    bool ok = payload && sf_load(payload, next);
    free(payload);
    FrameRect destination = fit_frame(info.width, info.height, 1920, 1080);
    vp_place(info.width, info.height, destination.x, destination.y, destination.w, destination.h);
    if (elogf) {
        fprintf(elogf, "native load=%d size=%dx%d fps=%d audio=%d\n", ok, info.width, info.height,
                info.fps, *info.audio_codec != 0);
        fflush(elogf);
    }
    if (!ok)
        copy_text(failure, sizeof failure, "电视拒绝了硬件解码请求");
    return ok;
}
bool media_start(const Config *config, const Asset *asset, char *error) {
    media_stop();
    failure[0] = 0;
    __atomic_store_n(&state, 0, __ATOMIC_RELEASE);
    started = held = loaded = attempted = eos = false;
    clock_base = 0;
    clock_start = SDL_GetTicks();
    fed = 0;
    memset(&info, 0, sizeof info);
    memset(&pending, 0, sizeof pending);
    __atomic_store_n(&rendered, 0, __ATOMIC_RELEASE);
    demux = demux_start(config, asset->id);
    if (!demux) {
        copy_text(error, ERROR_SIZE, "无法启动视频解析器");
        return false;
    }
    active = true;
    return true;
}
void media_stop(void) {
    if (!active)
        return;
    if (demux) {
        demux_stop(demux);
        demux = NULL;
    }
    demux_packet_free(&pending);
    if (loaded) {
        __atomic_add_fetch(&epoch, 1, __ATOMIC_ACQ_REL);
        sf_unload();
        uint32_t deadline = SDL_GetTicks() + 1500;
        while (!sf_unload_completed() && SDL_GetTicks() < deadline)
            SDL_Delay(5);
        sf_callback_gate_retire();
        sf_destroy();
        vp_destroy_window();
        if (elogf) {
            fprintf(elogf, "native stopped: packets=%u displayed=%u\n", fed,
                    __atomic_load_n(&rendered, __ATOMIC_ACQUIRE));
            fflush(elogf);
        }
    }
    active = started = held = loaded = false;
    __atomic_store_n(&state, 0, __ATOMIC_RELEASE);
}
double media_position(void) {
    return (!started || held) ? clock_base : clock_base + (SDL_GetTicks() - clock_start) / 1000.0;
}
void media_pause(bool paused) {
    clock_base = media_position();
    clock_start = SDL_GetTicks();
    held = paused;
    if (active && loaded) {
        if (paused)
            sf_pause();
        else
            sf_play();
    }
}
void media_seek(double seconds) {
    if (active && loaded && demux) {
        demux_packet_free(&pending);
        demux_seek(demux, seconds);
        sf_seek_ms((long long)(seconds * 1000));
        eos = false;
        clock_base = seconds;
        clock_start = SDL_GetTicks();
    }
}
int media_state(void) {
    if (active && demux) {
        if (demux_error(demux, failure))
            __atomic_store_n(&state, -1, __ATOMIC_RELEASE);
        if (!attempted && demux_info(demux, &info)) {
            attempted = true;
            loaded = load_native();
            if (!loaded)
                __atomic_store_n(&state, -1, __ATOMIC_RELEASE);
        }
        if (loaded && __atomic_load_n(&state, __ATOMIC_ACQUIRE) >= 0) {
            for (int i = 0; i < 16; ++i) {
                if (!pending.data && !demux_packet(demux, &pending))
                    break;
                char reply = sf_feed(pending.data, (unsigned)pending.size, pending.pts_ns,
                                     pending.audio ? 2 : 1);
                if (reply == 'B')
                    break;
                if (reply != 'O') {
                    copy_text(failure, sizeof failure, "电视未接受音视频数据");
                    __atomic_store_n(&state, -1, __ATOMIC_RELEASE);
                    break;
                }
                ++fed;
                demux_packet_free(&pending);
            }
            if (!started && fed && sf_is_load_completed()) {
                sf_play();
                started = true;
                clock_start = SDL_GetTicks();
            }
            if (!eos && !pending.data && demux_finished(demux)) {
                sf_push_eos();
                eos = true;
            }
        }
    }
    return __atomic_load_n(&state, __ATOMIC_ACQUIRE);
}
const char *media_error(void) {
    return *failure ? failure : "电视未能播放这段视频，请检查网络或文件格式。";
}
void media_shutdown(void) {
    media_stop();
    vp_destroy_window();
}
#endif
