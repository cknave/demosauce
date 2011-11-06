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

#include <algorithm>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/numeric/conversion/cast.hpp>

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
    #define AV_VERSION_INT(a, b, c) (a<<16 | b<<8 | c)
#endif

using std::string;
using boost::numeric_cast;
namespace fs = ::boost::filesystem;

typedef int16_t sample_t;

struct AvSource::Pimpl
{

    Pimpl() :
        format_context(0),
        codec_context(0),
        codec(0),
        audio_stream_index(-1),
        packet_buffer_pos(0),
        length(0)
    {}
    string codec_type();
    void free();
    int decode_frame(AVPacket& packet, uint8_t* const buffer, int const size);
    void process(AudioStream& stream, uint32_t frames);

    ConvertFromInterleaved<sample_t> converter;
    string file_name;

    AVFormatContext* format_context;
    AVCodecContext* codec_context;
    AVCodec* codec;

    int audio_stream_index;
    int packet_buffer_pos;
    uint64_t length;
    AlignedBuffer<uint8_t> packet_buffer;
    AlignedBuffer<uint8_t> decode_buffer;
};

AvSource::AvSource():
    pimpl(new Pimpl)
{
    av_register_all();
    pimpl->free();
}

bool AvSource::load(string file_name)
{
    pimpl->free();
    pimpl->file_name = file_name;
    LOG_DEBUG("[avsource] attempting to load %s", file_name.c_str());

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 7, 0)
    if (av_open_input_file(&pimpl->format_context, file_name.c_str(), 0, 0, 0) != 0) {
#else
    if (avformat_open_input(&pimpl->format_context, file_name.c_str(), 0, 0) != 0) {
#endif
        LOG_DEBUG("[avsource] can't load %s", file_name.c_str());
        return false;
    }

    if (av_find_stream_info(pimpl->format_context) < 0) {
        LOG_DEBUG("[avsource] no stream information %s", file_name.c_str());
        return false;
    }

    for (unsigned int i = 0; i < pimpl->format_context->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 7, 0)
        if (pimpl->format_context->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO) {
#else
        if (pimpl->format_context->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
#endif
            pimpl->audio_stream_index = i;
            break;
        }
    }

    if (pimpl->audio_stream_index == -1) {
        LOG_DEBUG("[avsource] no audio stream :( %s", file_name.c_str());
        return false;
    }

    pimpl->codec_context = pimpl->format_context->streams[pimpl->audio_stream_index]->codec;
    pimpl->codec = avcodec_find_decoder(pimpl->codec_context->codec_id);
    if (!pimpl->codec) {
        LOG_DEBUG("[avsource] unsupported codec %s", file_name.c_str());
        return false;
    }

    if (avcodec_open(pimpl->codec_context, pimpl->codec) < 0) {
        LOG_DEBUG("[avsource] failed to open codec %s", file_name.c_str());
        return false;
    }

    pimpl->length = numeric_cast<uint64_t>(pimpl->format_context->duration *
        (static_cast<double>(pimpl->codec_context->sample_rate) / AV_TIME_BASE));

    LOG_INFO("[avsource] playing %s", file_name.c_str());

    return true;
}

int AvSource::Pimpl::decode_frame(AVPacket& packet, uint8_t* const buffer, int const size)
{
    int len = 0;
    int decoded_size = 0;
    //store size & data for later freeing
    uint8_t* packet_data = packet.data;
    int packet_size = packet.size;

    while (packet.size > 0) {
        int data_size = size - decoded_size;
        if (decode_buffer.size() < static_cast<uint32_t>(size)) {// the decode buffer is needed due to alignment issues with sse
            decode_buffer.resize(size);
        }
        sample_t* buf = reinterpret_cast<sample_t*>(decode_buffer.get());

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 26, 0)
        len = avcodec_decode_audio2(codec_context, buf, &data_size, packet.data, packet.size);
#else
        len = avcodec_decode_audio3(codec_context, buf, &data_size, &packet);
#endif
        if (len < 0) // error, skip frame
            return 0;

        memmove(buffer + decoded_size, decode_buffer.get(), data_size);
        packet.data += len;
        packet.size -= len;
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
        if (av_read_frame(format_context, &packet) < 0) { // demux/read packet
            break; // end of stream
        }
        if (packet.stream_index != audio_stream_index) {
            continue;
        }
        packet_buffer_pos += decode_frame(packet, packet_buffer.get() + packet_buffer_pos, buffer_size);
    }

    // according to the docs, av_free_packet should be called at some point after av_read_frame
    // without these checks, mysterious segfaults start appearing with small buffers. stable my ass!
     if (packet.data != 0 && packet.size != 0) { // && packet.stream_index == audio_stream_index
        av_free_packet(&packet);
     }

    stream.end_of_stream = packet_buffer_pos < need_bytes;
    size_t used_bytes = std::min(need_bytes, packet_buffer_pos);
    uint32_t used_frames = bytes_in_frames<uint32_t, sample_t>(used_bytes, channels);
    sample_t* conv_buffer = converter.input_buffer(frames, channels);

    memmove(conv_buffer, packet_buffer.get(), used_bytes);
    converter.process(stream, used_frames);
    memmove(packet_buffer.get(), packet_buffer.get() + used_bytes, packet_buffer_pos - used_bytes);
    packet_buffer_pos -= used_bytes;

    if (stream.end_of_stream) {
        LOG_DEBUG("[avsource] eos avcodec %lu frames left", stream.frames());
    }
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

void AvSource::Pimpl::free()
{
    if (codec_context)
        avcodec_close(codec_context);
    if (format_context)
        av_close_input_file(format_context);
    codec = 0;
    codec_context = 0;
    format_context = 0;
    packet_buffer_pos = 0;
    length = 0;
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
    float br = (pimpl->codec_context == 0) ?
        0 :
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
    fs::path file(file_name);
    string name = file.string();
    static const size_t elements = 17;
    const char* ext[elements] = {".mp3", ".ogg", ".m4a", ".wma", ".acc", ".flac", ".mp4", ".ac3",
        ".wav", ".ape", ".wv", ".mpc", ".mp+", ".mpp", ".ra", ".mp2", ".mp1"};
    for (size_t i = 0; i < elements; ++i) {
        if (boost::iends_with(name, ext[i])) {
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
