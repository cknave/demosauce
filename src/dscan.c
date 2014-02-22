/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXIV by maep
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <getopt.h>
#include <chromaprint.h>
#include <replay_gain.h>
#include "bassdecoder.h"
#include "ffdecoder.h"
#include "effects.h"
#include "util.h"

enum {
    SAMPLERATE  = 44100,
    MAX_LENGTH  = 3600,             // abort scan if track is too long, in seconds
    FP_LENGTH   = 120,              // length of fingerprint 
    BUF_LENGTH  = SAMPLERATE / 20   // buffer size for sample format converter 
};

static const char* HELP_MESSAGE =
    "demosauce dscan tool 0.5.1"ID_STR"\n"                                   
    "syntax: scan [options] file\n"                                         
    "   -h                      print help\n"                               
    "   -r                      replaygain analysis\n" 
    "   -c                      acoustic fingerprint\n"      
    "   -o file.wav, stdout     write to wav or stdout\n"                   
    "                           format is 16 bit, 44.1 khz, stereo\n"       
    "                           stdout is raw data, and has no wav header";

// for some formats avcodec fails to provide a bitrate so I just
// make an educated guess. if the file contains large amounts of 
// other data besides music, this will be completely wrong.
static float fake_bitrate(const char* path, float duration)
{
    long size = util_filesize(path);
    return (size * 8) / (duration * 1000);
}

void die(const char* msg)
{
    puts(msg);
    exit(EXIT_FAILURE);
}

static void mwav_write_int(FILE* f, int v, int size)
{
    unsigned char buf[4] = {v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255};
    fwrite(buf, 1, size, f);
}

static FILE* mwav_open_writer(const char* path, int channels, int samplerate, int samplesize)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return NULL;
    fwrite("RIFF", 1, 4, f);
    mwav_write_int(f, 0, 4);            /* filled by close */
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    mwav_write_int(f, 16, 4);
    mwav_write_int(f, samplesize == 4 ? 3 : 1, 2); 
    mwav_write_int(f, channels, 2);
    mwav_write_int(f, samplerate, 4); 
    mwav_write_int(f, channels * samplerate * samplesize, 4);
    mwav_write_int(f, channels * samplesize, 2);
    mwav_write_int(f, samplesize * 8, 2);
    fwrite("data", 1, 4, f);
    mwav_write_int(f, 0, 4);            /* filled by close */
    if (ftell(f) == 44)
        return f;
    fclose(f);
    return NULL;
}

static void mwav_close_writer(FILE* f)
{
    if (!f)
        return;
    int size = (int)ftell(f);
    if (size < 0)
        size = INT_MAX;
    fseek(f, 4, SEEK_SET);
    mwav_write_int(f, size - 8, 4);
    fseek(f, 40, SEEK_SET);
    mwav_write_int(f, size - 44, 4);
    fclose(f);
}

// small helper function to interleave flaot to int16 samples
static int interleave(struct stream* s, int16_t* buf, int* frames)
{
    int process_frames = CLAMP(0, s->frames - *frames, BUF_LENGTH);
    const float* left  = s->buffer[0] + *frames;
    const float* right = s->buffer[s->channels == 1 ? 0 : 1] + *frames;
    for (int i = 0; i < process_frames; i++) {
        // TODO: swap bytes on big endian machines
        buf[i * 2]     = CLAMP(INT16_MIN, left[i]  * INT16_MAX, INT16_MAX);
        buf[i * 2 + 1] = CLAMP(INT16_MIN, right[i] * INT16_MAX, INT16_MAX);
    }
    *frames += process_frames;
    return process_frames;
}

static void write_wav(FILE* f, struct stream* s)
{
    int16_t tmp[BUF_LENGTH * 2];
    int frames = 0;
    while (frames < s->frames) {
        int bframes = interleave(s, tmp, &frames);
        fwrite(tmp, sizeof (int16_t), bframes * 2, f);
    }
}

static void fp_feed(ChromaprintContext* cp, struct stream* s)
{
    int16_t tmp[BUF_LENGTH * 2];
    int frames = 0;
    while (frames < s->frames) {
        int bframes = interleave(s, tmp, &frames);
        chromaprint_feed(cp, tmp, bframes * 2);
    }
}

int main(int argc, char** argv)
{
    bool    enable_rg   = false;
    bool    enable_fp   = false;
    FILE*   output      = NULL;

#ifdef ENABLE_BASS
    if (!bass_loadso())
        die("failed to load libbass.so");
#endif
    if (argc <= 1) 
        die(HELP_MESSAGE);
    
    char c = 0;
    while ((c = getopt(argc, argv, "hrco:-:")) != -1) {
        switch (c) {
        default:
        case '?':
            die(HELP_MESSAGE);
        case 'h':
            puts(HELP_MESSAGE);
            return EXIT_SUCCESS;
        case 'r':
            enable_rg = true;
            break;
        case 'c':
            enable_fp = true;
            break;
        case 'o':
            if (!strcmp(optarg, "stdout"))
                output = stdout;
            else 
                output = mwav_open_writer(optarg, 2, SAMPLERATE, 2);
            break;
        case '-':   // backwards compatible flag with 3.x, deprecated
            if (!strcmp(optarg, "no-replaygain"))
                enable_rg = false;
            else
                die(HELP_MESSAGE);
            break;
        };
    }
    const char* path = argv[optind];
    if (output == stdout) // no point in analyzing if output is active
        enable_fp = enable_rg = false; 
           
    struct decoder decoder = {0};
    bool loaded = false;
#ifdef ENABLE_BASS
    loaded = bass_load(&decoder, path, "bass_prescan=true", SAMPLERATE);
#endif
    if (!loaded)
        loaded = ff_load(&decoder, path);

    if (!loaded) 
        die("unknown format");
    
    struct info info = {0};
    decoder.info(&decoder, &info);

    if (info.samplerate <= 0) 
        die("bad samplerate");

    if (info.channels < 1 || info.channels > 2) 
        die("bad channel number");
    
    bool decode_audio = enable_rg || enable_fp || output;
    struct stream   stream0 = {{0}};
    struct stream   stream1 = {{0}};
    struct stream*  stream = &stream0;
    
    void* resampler = NULL;
    if (info.samplerate != SAMPLERATE && decode_audio) {
        resampler = fx_resample_init(info.channels, info.samplerate, SAMPLERATE);
        if (!resampler)
            die("failed to init resampler");      
        stream = &stream1; 
    }

    struct rg_context* rg_ctx = rg_new(SAMPLERATE, RG_FLOAT32, info.channels, false);
    
    ChromaprintContext* cp_ctx = chromaprint_new(CHROMAPRINT_ALGORITHM_DEFAULT);
    chromaprint_start(cp_ctx, SAMPLERATE, 2);

    // avcodec is unreliable when it comes to length, the only way to get accurate length 
    // is to decode the whole stream
    long frames = 0;
    if (decode_audio || (info.flags & INFO_FFMPEG)) {
        while (!stream->end_of_stream) {
            decoder.decode(&decoder, &stream0, SAMPLERATE);
            frames += stream0.frames;
            if (frames > MAX_LENGTH * info.samplerate) 
                die("exceeded maxium length");

            if (resampler)
                fx_resample(resampler, &stream0, &stream1);
                
            // there is a strange bug in the replaygain code that can cause it to report the wrong
            // value if the input buffer has an odd length, until the root of the cause is found,
            // & ~1 will have to do
            float* buff[2] = {stream->buffer[0], stream->buffer[1]};
            if (enable_rg) 
                rg_analyze(rg_ctx, buff, stream->frames & ~1);
                
            if (enable_fp && frames <= FP_LENGTH * info.samplerate)
                fp_feed(cp_ctx, stream);

            if (output)
                write_wav(output, stream);
        }
    }

    if (output == stdout)
        return EXIT_SUCCESS;
    if (output)
        mwav_close_writer(output);

    char* str = NULL;
    str = decoder.metadata(&decoder, "artist");
    if (str)
        printf("artist:%s\n", str);
        
    str = decoder.metadata(&decoder, "title");
    if (str)
        printf("title:%s\n", str);
        
    printf("type:%s\n", info.codec);
    
    // ffmpeg's length is not reliable
    float duration = (float)((info.flags & INFO_FFMPEG) ? frames : info.frames) / info.samplerate;
    printf("length:%f\n", duration);
    
    if (enable_rg)
        printf("replaygain:%f\n", rg_title_gain(rg_ctx));

#ifdef ENABLE_BASS
    if ((info.flags & INFO_BASS) && (info.flags & INFO_MOD))
        printf("loopiness:%f\n", bass_loopiness(path));
#endif

    if (info.bitrate)
        printf("bitrate:%f\n", info.bitrate);
    else if (info.flags & INFO_FFMPEG)
        printf("bitrate:%f\n", fake_bitrate(path, frames / info.samplerate));

    if (!(info.flags & INFO_MOD))
        printf("samplerate:%d\n", info.samplerate);
        
    if (enable_fp) {
        char* fingerprint = NULL;
        chromaprint_finish(cp_ctx);
        chromaprint_get_fingerprint(cp_ctx, &fingerprint);
        printf("acoustid:%s\n", fingerprint);  
    }
    
    return EXIT_SUCCESS;
}

