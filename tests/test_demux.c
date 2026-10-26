#include "api.h"
#include "demux.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    assert(argc == 4);
    Config config = {.allow_http = true};
    copy_text(config.server, sizeof config.server, argv[1]);
    copy_text(config.api_key, sizeof config.api_key, "immich-tv-demo");
    char error[ERROR_SIZE] = {0};
    assert(api_init(error));
    Demux *d = demux_start(&config, argv[2]);
    assert(d);
    DemuxInfo info = {0};
    for (int i = 0; i < 400 && !demux_info(d, &info); ++i) {
        assert(!demux_error(d, error));
        usleep(10000);
    }
    assert(info.width == 1280 && info.height == 720 && info.fps == 25);
    assert(!strcmp(info.video_codec, "H264"));
    bool expect_audio = !strcmp(argv[3], "audio");
    assert((*info.audio_codec != 0) == expect_audio);
    int video = 0, audio = 0;
    int64_t maximum = 0;
    for (int tries = 0; tries < 3000; ++tries) {
        MediaPacket p = {0};
        if (demux_packet(d, &p)) {
            assert(p.size > 4);
            if (p.audio) {
                assert(p.data[0] == 0xff && (p.data[1] & 0xf0) == 0xf0);
                ++audio;
            } else {
                assert(p.data[0] == 0 && p.data[1] == 0 &&
                       (p.data[2] == 1 || (p.data[2] == 0 && p.data[3] == 1)));
                ++video;
            }
            if (p.pts_ns > maximum)
                maximum = p.pts_ns;
            demux_packet_free(&p);
        } else if (demux_finished(d))
            break;
        else {
            assert(!demux_error(d, error));
            usleep(10000);
        }
    }
    assert(video == 300);
    assert(maximum > 11000000000LL);
    assert((audio > 0) == expect_audio);
    /* Seek after EOF: the worker must remain available, rather than disappearing
       while the native decoder still has buffered frames to present. */
    demux_seek(d, 5.0);
    bool resumed = false;
    for (int i = 0; i < 400; ++i) {
        MediaPacket p = {0};
        if (demux_packet(d, &p)) {
            resumed = true;
            demux_packet_free(&p);
            break;
        }
        assert(!demux_error(d, error));
        usleep(10000);
    }
    assert(resumed);
    demux_stop(d);
    d = demux_start(&config, argv[2]);
    assert(d);
    demux_stop(d);
    api_cleanup();
    printf("demux: video=%d audio=%d, Annex B / ADTS, seek after EOF and early stop passed\n",
           video, audio);
    return 0;
}
