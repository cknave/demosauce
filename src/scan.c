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
#include <getopt.h>
#include <replay_gain.h>
#include "bassdecoder.h"
#include "ffdecoder.h"
#include "effects.h"
#include "util.h"

#define MAX_LENGTH      3600     // abort scan if track is too long, in seconds
#define SAMPLERATE      44100
#define HELP_MESSAGE    "demosauce scan tool 0.4.0"ID_STR"\nsyntax: scan [options] file\n\t-h help\n\t-r no replaygain analysis"

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
    const char*     path        = NULL;
    bool            analyze     = true;
    bool            loaded      = false;
    struct info     info        = {0};
    struct decoder  decoder     = {0};
    void*           resampler   = NULL;
    struct stream   stream0     = {{0}};
    struct stream   stream1     = {{0}};
    struct stream*  stream      = &stream0;

#ifdef ENABLE_BASS
    if (!bass_loadso(argv))
        die("failed to load libbass.so");
#endif
    if (argc <= 1) 
        die(HELP_MESSAGE);
    
    char c = 0;
    while ((c = getopt(argc, argv, "hr")) != -1) {
        switch (c) {
        default:
        case '?':
            die(HELP_MESSAGE);
        case 'h':
            puts(HELP_MESSAGE);
            return EXIT_SUCCESS;
        case 'r':
            analyze = false;
            break;
        };
    }
    path = argv[optind];

#ifdef ENABLE_BASS
    loaded = bass_load(path, "bass_prescan=true", SAMPLERATE)))
#endif
    if (!loaded)
        loaded = ff_load(path)))

    if (!loaded) 
        die("unknown format");
        
    decoder.info(&decoder, &info);

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

    // avcodec is unreliable when it comes to length, so the only way to be 
    // absolutely accurate is to decode the whole stream
    long frames = 0;
    if (analyze || (info.flags & INFO_FFMPEG)) {
        while (!stream->end_of_stream) {
            decoder.decode(&decoder, &stream0, SAMPLERATE);
            // TODO disable resampler if rg is disabled
            if (resampler)
                fx_resample(resampler, &stream0, &stream1);
            float* buff[2] = {stream->buffer[0], stream->buffer[1]};
            // there is a strange bug in the replaygain code that can cause it to report the wrong
            // value if the input buffer has an odd lengh, until the root of the cause is found,
            // this will have to do :(
            if (analyze) 
                rg_analyze(ctx, buff, stream->frames & -2);
            frames += stream->frames;
            if (frames > MAX_LENGTH * SAMPLERATE) 
                die("exceeded max length");
        }
    }

    char* str = NULL;
    str = decoder.metadata(&decoder, "artist");
    if (str)
        printf("artist:%s\n", str);
    util_free(str);
    str = decoder.metadata(&decoder, "title");
    if (str)
        printf("title:%s\n", str); 
    util_free(str);
    printf("type:%s\n", info.codec);

    // ffmpeg's length is not reliable
    float duration = (info.flags & INFO_FFMPEG) ?
        (float)frames / SAMPLERATE :
        (float)info.frames / info.samplerate;
    printf("length:%f\n", duration);
    
    if (analyze)
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

    decoder.free(&decoder);
    stream_free(&stream0);
    stream_free(&stream1);
    fx_resample_free(resampler);
    
    return EXIT_SUCCESS;
}

