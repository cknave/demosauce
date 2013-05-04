/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <replay_gain.h>
#include "bassdecoder.h"
#include "ffdecoder.h"
#include "effects.h"
#include "util.h"

//#include "../../microwav/microwav.h"

#define MAX_LENGTH 3600     // abort scan if track is too long, in seconds
#define SAMPLERATE 44100

// for some formats avcodec fails to provide a bitrate so I just
// make an educated guess. if the file contains large amounts of 
// other data besides music, this will be completely wrong.
static float fake_bitrate(const char* path, float duration)
{
    long bytes = util_filesize(path);
    long kbit = (bytes * 8) / 1000;
    return (float)kbit / duration;
}

void die(const char* msg)
{
    puts(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
#ifdef ENABLE_BASS
    if (!bass_loadso(argv))
        return EXIT_FAILURE;
#endif
    if (argc < 2 || (!strcmp(argv[1], "--") && argc < 3)) {
        puts("demosauce scan tool 0.4.0"ID_STR"\nsyntax: scan [--no-replaygain] file");
        return EXIT_FAILURE;
    }
    const char*     path        = argv[argc - 1];
    bool            do_scan     = strcmp(argv[1], "--no-replaygain");
    struct info     info        = {0};
    void*           decoder     = NULL;
    void*           resampler   = NULL;
    struct stream   stream0     = {{0}};
    struct stream   stream1     = {{0}};
    struct stream*  stream      = &stream0;;

#ifdef ENABLE_BASS
    if ((decoder = bass_load(path, "bass_prescan=true", SAMPLERATE)))
        bass_info(decoder, &info);
#endif
    if (!decoder && (decoder = ff_load(path))) {
        ff_info(decoder, &info);
        do_scan = true;
    }

    if (!decoder) 
        die("unknown format");

    if (info.samplerate <= 0) 
        die("improper samplerate");

    if (info.channels < 1 || info.channels > 2) 
        die("improper channel number");
    
    if (info.samplerate != SAMPLERATE) {
        resampler = fx_resample_init(info.channels, info.samplerate, SAMPLERATE);
        if (!resampler)
            die("failed to init resampler");      
        stream = &stream1; 
    }

    struct rg_context* ctx = rg_new(SAMPLERATE, RG_FLOAT32, info.channels, false);

//    FILE* wav = mwav_open_writer("dump.wav", 1, SAMPLERATE, 4);
    // avcodec is unreliable when it comes to length, so the only way to be 
    // absolutely accurate is to decode the whole stream
    long frames = 0;
    if (do_scan || (info.flags & INFO_FFMPEG)) {
        while (!stream->end_of_stream) {
            info.decode(decoder, &stream0, SAMPLERATE);
            if (resampler)
                fx_resample(resampler, &stream0, &stream1);
            float* buff[2] = {stream->buffer[0], stream->buffer[1]};
//            fwrite(stream->buffer[0], 1, stream->frames * sizeof(float), wav);
            // there is a strange bug in the replaygain code that can cause it to report the wrong
            // value if the input buffer has an odd lengh, until the root of the cause is found,
            // this will have to do :(
            if (do_scan) 
                rg_analyze(ctx, buff, stream->frames & -2);
            frames += stream->frames;
            if (frames > MAX_LENGTH * SAMPLERATE) 
                die("exceeded max length");
        }
    }

    char* str = NULL;
    str = info.metadata(decoder, "artist");
    if (str)
        printf("artist:%s\n", str);
    util_free(str);
    str = info.metadata(decoder, "title");
    if (str)
        printf("title:%s\n", str); 
    util_free(str);
    printf("type:%s\n", info.codec);

    // ffmpeg's length is not reliable
    float duration = (info.flags & INFO_FFMPEG) ?
        (float)frames / SAMPLERATE :
        (float)info.frames / info.samplerate;
    printf("length:%f\n", duration);
    
    if (do_scan)
        printf("replaygain:%f\n", rg_title_gain(ctx));
    rg_free(ctx);
#ifdef ENABLE_BASS
    if ((info.flags & INFO_BASS) && (info.flags & INFO_MOD))
        printf("loopiness:%f\n", bass_loopiness(path));
#endif
    if (info.bitrate)
        printf("bitrate:%f\n", info.bitrate);
    else if (info.flags & INFO_FFMPEG)
        printf("bitrate:%f\n", fake_bitrate(path, frames * SAMPLERATE));
    if (!(info.flags & INFO_MOD) && info.samplerate)
        printf("samplerate:%d\n", info.samplerate);

    info.free(decoder);
    stream_free(&stream0);
    stream_free(&stream1);
    fx_resample_free(resampler);
//    mwav_close_writer(wav);
    return EXIT_SUCCESS;
}
