/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXIII by maep
*/

// fix missing UINT64_C macro
#define __STDC_CONSTANT_MACROS

#include <string.h>
#include <strings.h>
#include <samplerate.h>
#ifdef AVCODEC_FIX0
    #include <avcodec.h>
    #include <avformat.h>
#else
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
#endif
#include "log.h"
#include "effects.h"
#include "ffdecoder.h"

#ifndef AV_VERSION_INT 
    #define AV_VERSION_INT(a, b, c) (a << 16 | b << 8 | c)
#endif

struct ffdecoder {
    struct buffer       packet_buffer;
    struct buffer       decode_buffer;
    struct buffer       float_buffer;
    AVFormatContext*    format_context;
    AVCodecContext*     codec_context;
    AVCodec*            codec;
    int                 stream_index;
    int                 packet_buffer_pos;
    int                 channels;
    long                frames;
};

static bool initialized = false;

static void ff_free2(struct ffdecoder* d)
{
    if (d->codec_context)
        avcodec_close(d->codec_context);
    if (d->format_context)
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 25, 0)
        av_close_input_file(d->format_context);
#else
        avformat_close_input(&d->format_context);
#endif
}

void ff_free(void* handle)
{
    ff_free2(handle);
    util_free(handle);
}

void* ff_load(const char* path)
{
    if (!initialized) {
        av_register_all();
        initialized = true;
    }

    LOG_DEBUG("[ffdecoder] loading %s", path);
    
    int err = 0;
    struct ffdecoder d = {{0}};
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 4, 0)
    err = av_open_input_file(&d.format_context, path, 0, 0, 0);
#else
    err = avformat_open_input(&d.format_context, path, 0, 0);
#endif
    if (err) 
        goto error;
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 6, 0)
    err = av_find_stream_info(d.format_context);
#else
    err = avformat_find_stream_info(d.format_context, NULL);
#endif
    if (err < 0) 
        goto error;

    for (unsigned i = 0; i < d.format_context->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 64, 0)
        if (d.format_context->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO)
#else
        if (d.format_context->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
#endif
            d.stream_index = i;
            break;
    }
    if (d.stream_index == -1)
        goto error;

    d.codec_context = d.format_context->streams[d.stream_index]->codec;
    d.codec = avcodec_find_decoder(d.codec_context->codec_id);
    if (!d.codec) 
        goto error;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 8, 0)
    if (avcodec_open(d.codec_context, d.codec) < 0)
#else
    if (avcodec_open2(d.codec_context, d.codec, NULL) < 0)
#endif
        goto error;
    
    if (d.format_context->duration > 0) 
        d.frames = d.format_context->duration * d.codec_context->sample_rate / AV_TIME_BASE;
    
    struct ffdecoder* dec = util_malloc(sizeof(struct ffdecoder));
    memmove(dec, &d, sizeof(struct ffdecoder));
    LOG_INFO("[ffdecoder] loaded %s", path);
    return dec;

error:
    ff_free2(&d);
    LOG_DEBUG("[ffdecoder] failed to open %s", path);
    return NULL;
}

static int decode_frame(struct ffdecoder* d, AVPacket* p, char* buffer, int size)
{
    int ret = 0, decoded_size = 0;
    //store size & data for later freeing
    unsigned char* packet_data = p->data;
    int packet_size = p->size;

    while (p->size > 0) {
        int data_size = size - decoded_size;
        if (d->decode_buffer.size < size) // the decode buffer is needed due to alignment issues with sse
            buffer_resize(&d->decode_buffer, size);

        int16_t* buf = d->decode_buffer.data;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 23, 0)
        ret = avcodec_decode_audio2(d->codec_context, buf, &data_size, p->data, p->size);
#else
        ret = avcodec_decode_audio3(d->codec_context, buf, &data_size, p);
#endif
        // avcodec_decode_audio4 works completely different so I will keep that for the rewrite
        if (ret < 0) // error, skip frame
            return 0;
        memmove(buffer + decoded_size, buf, data_size);
        
        p->data += ret;
        p->size -= ret;
        decoded_size += data_size;
    }

    p->data = packet_data;
    p->size = packet_size;
    return decoded_size;
}

void ff_decode(void* handle, struct stream* s, int frames)
{
    struct ffdecoder* d = handle;

    if (d->packet_buffer_pos < 0) {
        LOG_ERROR("[ffdecoder] dirr tidledi derp");
        stream_zero(s, 0, frames);
        s->end_of_stream = true;
        return;
    }

    int need_bytes  = frames * s->channels * sizeof(int16_t); 
    int min_bytes   = MAX(192000, need_bytes); // small buffers seem to cause trouble!
    int buffer_size = MIN(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3, min_bytes * 2);
    if (d->packet_buffer.size < buffer_size)
        buffer_resize(&d->packet_buffer, buffer_size);
    
    AVPacket packet = {0};
    while (d->packet_buffer_pos < min_bytes) {
        if (av_read_frame(d->format_context, &packet) < 0) // demux/read packet
            break; // end of stream
        if (packet.stream_index != d->stream_index) 
            continue;
        d->packet_buffer_pos += decode_frame(d, &packet, (char*)d->packet_buffer.data + d->packet_buffer_pos, buffer_size);
    }

    // according to the docs, av_free_packet should be called at some point after av_read_frame
    // without these checks, mysterious segfaults start appearing with small buffers. stable my ass!
    if (packet.data != 0 && packet.size != 0) 
        av_free_packet(&packet);

    s->end_of_stream = d->packet_buffer_pos < need_bytes;
    int used_bytes = MIN(need_bytes, d->packet_buffer_pos);
    int used_frames = used_bytes / (d->channels * sizeof(int16_t));

    buffer_resize(&d->float_buffer, d->channels * used_frames * sizeof(float));
    src_short_to_float_array(d->packet_buffer.data, d->float_buffer.data, d->channels * used_frames);
    stream_fill(s, d->float_buffer.data, used_frames, d->channels);
    
    memmove(d->packet_buffer.data, (char*)d->packet_buffer.data + used_bytes, d->packet_buffer_pos - used_bytes);
    d->packet_buffer_pos -= used_bytes;

    if (s->end_of_stream) 
        LOG_DEBUG("[ffdecoder] eos avcodec %d frames left", s->frames);
}

void ff_seek(void* handle, long frame)
{
    struct ffdecoder* d = handle;
    int sr = d->codec_context->sample_rate;
    int64_t timestamp = frame / sr * AV_TIME_BASE;
    if (av_seek_frame(d->format_context, -1, timestamp, 0) < 0)
        LOG_WARN("[ffdecoder] seek failed");
}

static const char* codec_type(struct ffdecoder* d)
{
    enum CodecID codec_type = d->codec->id;
    if (codec_type >= CODEC_ID_PCM_S16LE && codec_type < CODEC_ID_ADPCM_IMA_QT) 
        return "pcm";
    if (codec_type >= CODEC_ID_ADPCM_IMA_QT && codec_type < CODEC_ID_AMR_NB) 
        return "adpcm";
    switch (codec_type) {
        case CODEC_ID_RA_144:
        case CODEC_ID_RA_288:   return "real";
        case CODEC_ID_MP2:      return "mp2";
        case CODEC_ID_MP3:      return "mp3";
        case CODEC_ID_AAC:      return "aac";
        case CODEC_ID_AC3:      return "ac3";
        case CODEC_ID_VORBIS:   return "vorbis";
#if LIBAVCODEC_VERSION_INT > AV_VERSION_INT(51, 50, 0)
        case CODEC_ID_WMAVOICE:
        case CODEC_ID_WMAPRO:
        case CODEC_ID_WMALOSSLESS: 
#endif
        case CODEC_ID_WMAV1:
        case CODEC_ID_WMAV2:    return "wma";
        case CODEC_ID_FLAC:     return "flac";
        case CODEC_ID_WAVPACK:  return "wavpack";
        case CODEC_ID_APE:      return "monkey";
        case CODEC_ID_MUSEPACK7:
        case CODEC_ID_MUSEPACK8:return "musepack";
#if LIBAVCODEC_VERSION_INT > AV_VERSION_INT(54, 0, 0)
        case CODEC_ID_OPUS:     return "opus";
#endif
        default:                return "unknown";
    }
}

void ff_info(void* handle, struct info* info)
{
    struct ffdecoder* d = handle;
    memset(info, 0, sizeof(struct info));
    info->decode        = ff_decode;
    info->free          = ff_free;         
    info->metadata      = ff_metadata;
    info->frames        = d->frames;
    info->codec         = codec_type(d);
    info->bitrate       = d->codec_context->bit_rate / 1000.0f;
    info->channels      = d->codec_context->channels;
    info->samplerate    = d->codec_context->sample_rate;
    info->flags         = INFO_FFMPEG | INFO_SEEKABLE;
}

bool ff_probe_name(const char* file_name)
{
    const char* ext[] = {".mp3", ".ogg", ".mp4", ".m4a" ".aac", ".wma", ".acc", ".flac", 
        ".ac3", ".wav", ".ape", ".wv", ".mpc", ".mp+", ".mpp", ".ra", ".mp2"
#if LIBAVCODEC_VERSION_INT > AV_VERSION_INT(54, 0, 0)
        , ".opus"
#endif
    }; 
    for (int i = 0; i < COUNT(ext); i++) {
        const char* tmp = strrchr(file_name, '.');
        if (!strcasecmp(tmp, ext[i])) 
            return true;
    }
    return false;
}

char* ff_metadata(void* handle, const char* key)
{
    struct ffdecoder* d = (struct ffdecoder*)handle;
    const char* value = NULL;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 7, 0)
    if (!strcmp(key, "artist"))
        value = d->format_context->author;
    else if (!strcmp(key, "title"))
        value = d->format_context->title;
#else
    AVDictionary* dict = d->format_context->metadata;
    value = av_dict_get(dict, key, 0, 0)->value;
#endif
    char* v = util_strdup(value);
    util_trim(v);
    return v;
}

