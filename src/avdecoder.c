/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*
*   there are some version check in this file that might be a bit off,
*   because I don't know exactly which verisons added support for them.
*   if you encounter errors, feel plase send me a patch or contact me.
*/

// fix missing UINT64_C macro
#define __STDC_CONSTANT_MACROS

#include <stdbool.h>
#include "logror.h"
#include "convert.h"
#include "avsource.h"

extern "C" {
#ifdef AVCODEC_FIX0
    #include <avcodec.h>
    #include <avformat.h>
#else
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
#endif
}

#ifndef AV_VERSION_INT 
    #define AV_VERSION_INT(a, b, c) (a << 16 | b << 8 | c)
#endif

struct avsource {
    struct decoder      decoder;
    struct buffer       packet_buffer;
    struct buffer       decode_buffer;
    const char*         file_name;
    AVFormatContext*    format_context;
    AVCodecContext*     codec_context;
    AVCodec*            codec;
    int                 stream_index;
    int                 packet_buffer_pos;
    long                length;
};

static bool initialized = false;

void avsource_free(struct avsource* as)
{
    if (as->codec_context)
        avcodec_close(codec_context);
    if (as->format_context)
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 25, 0)
        av_close_input_file(as->format_context);
#else
        avformat_close_input(&as->format_context);
#endif
    free(as->file_name);
}

bool avsource_load(struct decoder* dec, const char* file_name)
{
    struct avdecoder* d = (struct avdecoder*)dec;
    int err = 0;

    if (!initialized) {
        av_register_all();
        initialized = true;
    }

    avsource_free(as);
    d->file_name = strdup(file_name);
    LOG_DEBUG("[avsource] loading %s", file_name);

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 4, 0)
    err = av_open_input_file(&d->format_context, file_name, 0, 0, 0);
#else
    err = avformat_open_input(&d->format_context, file_name, 0, 0);
#endif
    if (err) {
        LOG_DEBUG("[avsource] can't load %s", file_name);
        return false;
    }
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(53, 6, 0)
    err = av_find_stream_info(d->format_context);
#else
    err = avformat_find_stream_info(d->format_context, NULL);
#endif
    if (err < 0) {
        LOG_DEBUG("[avsource] no stream information %s", file_name);
        return false;
    }

    for (unsigned i = 0; i < d->format_context->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 64, 0)
        if (d->format_context->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO) {
#else
        if (d->format_context->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
#endif
            d->stream_index = i;
            break;
        }
    }

    if (d->stream_index == -1) {
        LOG_DEBUG("[avsource] no audio stream :( %s", file_name);
        return false;
    }

    as->codec_context = as->format_context->streams[as->stream_index]->codec;
    as->codec = avcodec_find_decoder(as->codec_context->codec_id);
    if (!as->codec) {
        LOG_DEBUG("[avsource] unsupported codec %s", file_name);
        return false;
    }
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 8, 0)
    if (avcodec_open(as->codec_context, as->codec) < 0) {
#else
    if (avcodec_open2(as->codec_context, as->codec, NULL) < 0) {
#endif
        LOG_DEBUG("[avsource] failed to open codec %s", file_name);
        return false;
    }
    
    if (as->format_context->duration > 0) 
        as->length = as->format_context->duration * as->codec_context->sample_rate / AV_TIME_BASE;
    
    LOG_INFO("[avsource] playing %s", file_name);

    return true;
}

int AvSource::Pimpl::decode_frame(AVPacket& packet, uint8_t* const buffer, int const size)
{
    int ret = 0, decoded_size = 0;
    //store size & data for later freeing
    uint8_t* packet_data = packet.data;
    int packet_size = packet.size;

    while (packet.size > 0) {
        int data_size = size - decoded_size;
        if (decode_buffer.size() < static_cast<uint32_t>(size)) { // the decode buffer is needed due to alignment issues with sse
            decode_buffer.resize(size);
        }

        sample_t* buf = reinterpret_cast<sample_t*>(decode_buffer.get());
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 23, 0)
        ret = avcodec_decode_audio2(codec_context, buf, &data_size, packet.data, packet.size);
#else
        ret = avcodec_decode_audio3(codec_context, buf, &data_size, &packet);
#endif
        // avcodec_decode_audio4 works completely different so I will keep that for the rewrite

        if (ret < 0) // error, skip frame
            return 0;
        memmove(buffer + decoded_size, decode_buffer.get(), data_size);
        
        packet.data += ret;
        packet.size -= ret;
        decoded_size += data_size;
    }

    packet.data = packet_data;
    packet.size = packet_size;
    return decoded_size;
}

void AvSource::Pimpl::process(AudioStream& stream, uint32_t frames)
{
    uint32_t const channels = numeric_cast<uint32_t>(codec_context->channels);
    if (packet_buffer_pos < 0 || channels < 1) {
        ERROR("[avsource] dirr tidledi derp");
        stream.zero(0, frames);
        stream.end_of_stream = true;
        return;
    }

    int const need_bytes = frames_in_bytes<sample_t>(frames, channels);
    // somehow small buffers seem to cause trouble, so INCREASE BUFFER SIZE BEYOND REASON!
    // btw that was a reference to thumbtanic
    int const min_bytes = std::max(192000, need_bytes);
    int const buffer_size = std::max(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3, min_bytes * 2);
    if (packet_buffer.size() < static_cast<size_t>(buffer_size)) {
        packet_buffer.resize(buffer_size);
    }

    AVPacket packet;
    while (packet_buffer_pos < min_bytes) {
        if (av_read_frame(format_context, &packet) < 0) // demux/read packet
            break; // end of stream
        if (packet.stream_index != audio_stream_index) 
            continue;
        packet_buffer_pos += decode_frame(packet, packet_buffer.get() + packet_buffer_pos, buffer_size);
    }

    // according to the docs, av_free_packet should be called at some point after av_read_frame
    // without these checks, mysterious segfaults start appearing with small buffers. stable my ass!
    if (packet.data != 0 && packet.size != 0) 
        av_free_packet(&packet);
     

    stream.end_of_stream = packet_buffer_pos < need_bytes;
    size_t used_bytes = std::min(need_bytes, packet_buffer_pos);
    uint32_t used_frames = bytes_in_frames<uint32_t, sample_t>(used_bytes, channels);
    sample_t* conv_buffer = converter.input_buffer(frames, channels);

    memmove(conv_buffer, packet_buffer.get(), used_bytes);
    converter.process(stream, used_frames);
    memmove(packet_buffer.get(), packet_buffer.get() + used_bytes, packet_buffer_pos - used_bytes);
    packet_buffer_pos -= used_bytes;

    if (stream.end_of_stream) 
        LOG_DEBUG("[avsource] eos avcodec %lu frames left", stream.frames());
}

void AvSource::seek(uint64_t frame)
{
    int64_t timestamp = frame / samplerate() * AV_TIME_BASE;
    av_seek_frame(pimpl->format_context, -1, timestamp, 0);
}

void AvSource::process(AudioStream& stream, uint32_t frames)
{
    pimpl->process(stream, frames);
}
AvSource::~AvSource()
{
    pimpl->free();
}

string AvSource::name() const
{
    return "AvCodec Source";
}

uint32_t AvSource::channels() const
{
    uint32_t ch = (pimpl->codec_context == 0) ?
        0 :
        numeric_cast<uint32_t>(pimpl->codec_context->channels);
    return ch;
}

uint32_t AvSource::AvSource::samplerate() const
{
    uint32_t sr = (pimpl->codec_context == 0) ?
        44100 :
        numeric_cast<uint32_t>(pimpl->codec_context->sample_rate);
    return sr;
}

float AvSource::bitrate() const
{
    float br = (pimpl->codec_context == 0) ? 0 :
        numeric_cast<float>(pimpl->codec_context->bit_rate) / 1000;
    return  br;
}

uint64_t AvSource::length() const
{
    return pimpl->length;
}

bool AvSource::seekable() const
{
    return true;
}

bool AvSource::probe_name(string file_name)
{
    const char* ext[] = {".mp3", ".ogg", ".m4a", ".wma", ".acc", ".flac", ".mp4", ".ac3",
        ".wav", ".ape", ".wv", ".mpc", ".mp+", ".mpp", ".ra", ".mp2", ".mp1"};
    for (int i = 0; i < boost::size(ext); i++) {
        if (boost::iends_with(file_name, ext[i])) {
            return true;
        }
    }
    return false;
}

string AvSource::metadata(string key) const
{
    string value;
    if (key == "codec_type") {
        value = pimpl->codec_type();
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 7, 0)
    } else if (key == "artist") {
        value = pimpl->format_context->author;
    } else if (key == "title") {
        value = pimpl->format_context->title;
#else
    } else {
        AVDictionary* d = pimpl->format_context->metadata;
        value = av_dict_get(d, key.c_str(), 0, 0)->value;
#endif
    }
    boost::trim(value);
    return value;
}

string AvSource::Pimpl::codec_type()
{
    CodecID codec_type = codec->id;
    if (codec_type >= CODEC_ID_PCM_S16LE && codec_type < CODEC_ID_ADPCM_IMA_QT) {
        return "pcm";
    }
    if (codec_type >= CODEC_ID_ADPCM_IMA_QT && codec_type < CODEC_ID_AMR_NB) {
        return "adpcm";
    }   
    switch (codec_type) {
        case CODEC_ID_RA_144:
        case CODEC_ID_RA_288:   return "real";
        case CODEC_ID_MP2:      return "mp2";
        case CODEC_ID_MP3:      return "mp3";
        case CODEC_ID_AAC:      return "aac";
        case CODEC_ID_AC3:      return "ac3";
        case CODEC_ID_VORBIS:   return "vorbis";
        case CODEC_ID_WMAV1:
        case CODEC_ID_WMAV2:
#if LIBAVCODEC_VERSION_INT > AV_VERSION_INT(51, 50, 0)
        case CODEC_ID_WMAVOICE:
        case CODEC_ID_WMAPRO:
        case CODEC_ID_WMALOSSLESS: 
#endif
                                return "wma";
        case CODEC_ID_FLAC:     return "flac";
        case CODEC_ID_WAVPACK:  return "wavpack";
        case CODEC_ID_APE:      return "monkey";
        case CODEC_ID_MUSEPACK7:
        case CODEC_ID_MUSEPACK8: 
                                return "musepack";
#if LIBAVCODEC_VERSION_INT > AV_VERSION_INT(51, 50, 0)
        case CODEC_ID_MP1:      return "mp1";
#endif
// TODO not sure this is the right revision number
#if LIBAVCODEC_VERSION_INT > AV_VERSION_INT(52, 26, 0)
        case CODEC_ID_MP4ALS:   return "mp4";
#endif
        default:                return "unimportant";
    }
}
