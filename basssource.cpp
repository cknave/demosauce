/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <cstring>
#include <limits>
#include <algorithm>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include <id3tag.h>

#include "bass/bass.h"

#include "logror.h"
#include "keyvalue.h"
#include "convert.h"
#include "basssource.h"

using std::string;
using std::numeric_limits;

using boost::to_lower;
using boost::iends_with;
using boost::istarts_with;
using boost::numeric_cast;

namespace fs = ::boost::filesystem;

#ifdef BASSSOURCE_16BIT_DECODING
    typedef int16_t sample_t;
    #define FLOAT_FLAG 0
#else
    typedef float sample_t;
    #define FLOAT_FLAG BASS_SAMPLE_FLOAT
#endif

struct BassSource::Pimpl
{
    bool load(string file_name, bool prescan);
    void free();
    string codec_type() const;
    ConvertFromInterleaved<sample_t> converter;
    DWORD channel;
    BASS_CHANNELINFO channelInfo;
    string file_name;
    uint32_t samplerate;
    uint64_t currentFrame;
    uint64_t lastFrame;
};

BassSource::BassSource():
    pimpl(new Pimpl)
{
    pimpl->samplerate = 44100;
    pimpl->channel = 0;
    pimpl->free();
    BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 0);
    if (!BASS_Init(0, 44100, 0, 0, NULL) && BASS_ErrorGetCode() != BASS_ERROR_ALREADY) {
        FATAL("[basssource] init failed (%s)", BASS_ErrorGetCode());
    }
}

BassSource::~BassSource()
{
    pimpl->free();
}

bool BassSource::load(string file_name)
{
    bool loaded = pimpl->load(file_name, false);

    static DWORD const amiga_flags = BASS_MUSIC_NONINTER | BASS_MUSIC_PT1MOD;
    if (is_amiga_module()) {
        BASS_ChannelFlags(pimpl->channel, amiga_flags, amiga_flags);
    } else if (is_module()) {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_RAMP, BASS_MUSIC_RAMP);
    }

    return loaded;
}

bool BassSource::load(string file_name, string options)
{
    bool prescan = get_value(options, "bass_prescan", false);
    bool loaded = pimpl->load(file_name, prescan);
    if (!is_module()) 
        return loaded;

    // interpolation, values: auto, auto, off, linear, sinc (bass uses linear as default)
    string inter_str = get_value(options, "bass_inter", "auto");
    if ((is_amiga_module() && inter_str == "auto") || inter_str == "off") {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_NONINTER, BASS_MUSIC_NONINTER);
    }
    else if (inter_str == "sinc") {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_SINCINTER, BASS_MUSIC_SINCINTER);
    }

    // ramping, values: auto, normal, sensitive
    string ramp_str = get_value(options, "bass_ramp", "auto");
    if ((!is_amiga_module() && ramp_str == "auto") || inter_str == "normal") {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_RAMP, BASS_MUSIC_RAMP);
    } else if (ramp_str == "sensitive") {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_RAMPS, BASS_MUSIC_RAMPS);
    }

    // playback mode, values: auto, bass, pt1, ft2 (bass is default)
    string mode_str = get_value(options, "bass_mode", "auto");
    if ((is_amiga_module() && mode_str == "auto") || mode_str == "pt1") {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_PT1MOD, BASS_MUSIC_PT1MOD);
    } else if (mode_str == "ft2") {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_FT2MOD, BASS_MUSIC_FT2MOD);
    }

    return loaded;
}

bool BassSource::Pimpl::load(string file_name, bool prescan)
{
    free();
    DWORD stream_flags = BASS_STREAM_DECODE | (prescan ? BASS_STREAM_PRESCAN : 0) | FLOAT_FLAG;
    DWORD music_flags = BASS_MUSIC_DECODE | BASS_MUSIC_PRESCAN | FLOAT_FLAG;

    LOG_DEBUG("[basssource] attempting to load %s", file_name.c_str());
    channel = BASS_StreamCreateFile(FALSE, file_name.c_str(), 0, 0, stream_flags);
    if (!channel) {
        channel = BASS_MusicLoad(FALSE, file_name.c_str(), 0, 0 , music_flags, samplerate);
    }
    if (!channel) {
        LOG_DEBUG("[basssource] can't load %s", file_name.c_str());
        return false;
    }

    BASS_ChannelGetInfo(channel, &channelInfo);
    const QWORD length = BASS_ChannelGetLength(channel, BASS_POS_BYTE);
    lastFrame = length / (sizeof(sample_t) * channelInfo.chans);

    if (length == static_cast<QWORD>(-1)) {
        free();
        return false;
    }
    this->file_name = file_name;
    LOG_INFO("[basssource] playing %s", file_name.c_str());
    return true;
}

void BassSource::Pimpl::free()
{
    if (channel != 0) {
        if (channelInfo.ctype & BASS_CTYPE_MUSIC_MOD) {
            BASS_MusicFree(channel);
        } else {
            BASS_StreamFree(channel);
        }
        if (BASS_ErrorGetCode() != BASS_OK) {
             LOG_WARN("[basssource] failed to free channel (%d)", BASS_ErrorGetCode());
        }
        channel = 0;
    }
    file_name.clear();
    currentFrame = 0;
    lastFrame = 0;
    memset(&channelInfo, 0, sizeof(BASS_CHANNELINFO));
}

void BassSource::process(AudioStream& stream, uint32_t frames)
{
    uint32_t const chan = channels();
    if (chan != 2 && chan != 1) {
        ERROR("[basssource] unsupported number of channels");
        stream.end_of_stream = true;
        stream.set_frames(0);
        return;
    }

    uint32_t const framesToRead = unsigned_min<uint32_t>(frames, pimpl->lastFrame - pimpl->currentFrame);
    if (framesToRead == 0) {
        stream.end_of_stream = true;
        stream.set_frames(0);
        return;
    }

    DWORD const bytesToRead = frames_in_bytes<sample_t>(framesToRead, chan);
    sample_t* const readBuffer = pimpl->converter.input_buffer(frames, chan);
    DWORD const bytesRead = BASS_ChannelGetData(pimpl->channel, readBuffer, bytesToRead);

    if (bytesRead == static_cast<DWORD>(-1) && BASS_ErrorGetCode() != BASS_ERROR_ENDED) {
        ERROR("[basssource] failed to read from channel (%d)"), BASS_ErrorGetCode();
    }

    uint32_t framesRead = 0;
    if (bytesRead != static_cast<DWORD>(-1)) {
        framesRead = bytes_in_frames<uint32_t, sample_t>(bytesRead, chan);
    }

    // converter will set stream size
    pimpl->converter.process(stream, framesRead);
    pimpl->currentFrame += framesRead;

    stream.end_of_stream = framesRead != framesToRead || pimpl->currentFrame >= pimpl->lastFrame;
    if(stream.end_of_stream) {
        LOG_DEBUG("[basssource] eos bass %lu frames left", stream.frames());
    }
}

void BassSource::seek(uint64_t position)
{
    // implement me!
}

void BassSource::set_samplerate(uint32_t samplerate)
{
    pimpl->samplerate = samplerate;
}

void BassSource::set_loop_duration(double duration)
{
    if (pimpl->channel == 0)
        return;
    pimpl->lastFrame = numeric_cast<uint64_t>(duration * samplerate());
    BASS_ChannelFlags(pimpl->channel, BASS_SAMPLE_LOOP, BASS_SAMPLE_LOOP);
}

bool BassSource::is_module() const
{
    return pimpl->channelInfo.ctype & BASS_CTYPE_MUSIC_MOD;
}

bool BassSource::is_amiga_module() const
{
    return pimpl->channelInfo.ctype == BASS_CTYPE_MUSIC_MOD;
}

uint32_t BassSource::channels() const
{
    return static_cast<uint32_t>(pimpl->channelInfo.chans);
}

uint32_t BassSource::samplerate() const
{
    uint32_t value = (pimpl->channelInfo.freq == 0) ?
        44100 :
        static_cast<uint32_t>(pimpl->channelInfo.freq);
    return value;
}

float BassSource::bitrate() const
{
    if (is_module()) {
        return 0;
    }
    double size = BASS_StreamGetFilePosition(pimpl->channel, BASS_FILEPOS_END);
    double duration = static_cast<double>(length()) / samplerate();
    double bitrate = size / (125 * duration);
    return bitrate;
}

uint64_t BassSource::length() const
{
    return pimpl->lastFrame;
}

bool BassSource::seekable() const
{
    return false;
}

bool BassSource::probe_name(string file_name)
{
    fs::path file(file_name);
    string name = file.filename();

    static size_t const elements = 14;
    char const * ext[elements] = {".mp3", ".ogg", ".mp2", ".mp1", ".wav", ".aiff", ".xm", ".mod", ".s3m", ".it", ".mtm", ".umx", ".mo3", ".fst"};
    for (size_t i = 0; i < elements; ++i) {
        if (iends_with(name, ext[i])) {
            return true;
        }
    }

    // extrawurst for AMP :D
    static size_t const elements_amp = 8;
    char const * ext_amp[elements_amp] = {"xm.", "mod.", "s3m.", "it.", "mtm.", "umx.", "mo3.", "fst."};
    for (size_t i = 0; i < elements_amp; ++i) {
        if (istarts_with(name, ext_amp[i])) {
            return true;
        }
    }

    return false;
}

string BassSource::Pimpl::codec_type() const
{
    switch (channelInfo.ctype)
    {
        case BASS_CTYPE_STREAM_OGG: return "vorbis";
        case BASS_CTYPE_STREAM_MP1: return "mp1";
        case BASS_CTYPE_STREAM_MP2: return "mp2";
        case BASS_CTYPE_STREAM_MP3: return "mp3";
        case BASS_CTYPE_STREAM_AIFF:
        case BASS_CTYPE_STREAM_WAV_PCM:
        case BASS_CTYPE_STREAM_WAV_FLOAT:
        case BASS_CTYPE_STREAM_WAV: return "pcm";

        case BASS_CTYPE_MUSIC_MOD:  return "mod";
        case BASS_CTYPE_MUSIC_MTM:  return "mtm";
        case BASS_CTYPE_MUSIC_S3M:  return "s3m";
        case BASS_CTYPE_MUSIC_XM:   return "xm";
        case BASS_CTYPE_MUSIC_IT:   return "it";
        case BASS_CTYPE_MUSIC_MO3:  return "mo3";
        default:;
    }
    return "-";
}

string get_id3_tag(const char* tags, string key)
{
    const TAG_ID3* tag = reinterpret_cast<const TAG_ID3*>(tags);
    string value;
    if (key == "artist") {
        value.assign(tag->artist, 30);
    } else if (key == "title") {
        value.assign(tag->title, 30);
    }
    return value;
}

// and the award to the bested documented lib goes to: libid3tag! </irony>
string get_id3v2_tag(const char* tags, string key)
{
    const id3_byte_t* data = reinterpret_cast<const id3_byte_t*>(tags);
    string value;
    const char* frame_id = 0;
    signed long length = 0;
    id3_tag* tag = 0;
    id3_frame* frame = 0;
    id3_field* field = 0;
    const id3_ucs4_t* ucs_str = 0;
    id3_utf8_t* utf_str = 0;

    if (key == "artist") {
        frame_id = ID3_FRAME_ARTIST;
    } else if (key == "title") {
        frame_id = ID3_FRAME_TITLE;
    } else {
        goto id3fn_quit;
    }

    length = id3_tag_query(data, ID3_TAG_QUERYSIZE);
    if (length == 0) {
        goto id3fn_quit;
    }

    tag = id3_tag_parse(data, length);
    if (tag == 0) {
        goto id3fn_quit;
    }

    frame = id3_tag_findframe(tag, frame_id, 0);
    if (frame == 0) {
        goto id3fn_quit_tag;
    }

    field = id3_frame_field(frame, 1);
    ucs_str = id3_field_getstrings(field, 0);
    if (ucs_str == 0) {
        goto id3fn_quit_frame;
    }

    utf_str = id3_ucs4_utf8duplicate(ucs_str);
    value = reinterpret_cast<char*>(utf_str);
    free(utf_str);

id3fn_quit_frame:
    id3_frame_delete(frame);
id3fn_quit_tag:
    id3_tag_delete(tag);
id3fn_quit:
    return value;
}

string get_ogg_tag(const char* tags, string key)
{
    for (size_t i = 0; tags[i] != 0;) {
        string data = tags + i;
        if (istarts_with(data, key) && tags[i + key.size()] == '=') {
            return data.substr(key.size() + 1);
        }
        i += data.size() + 1;
    }
    return "";
}

string get_tag(DWORD handle, string key)
{
    const char* tags = BASS_ChannelGetTags(handle, BASS_TAG_ID3V2);
    if (tags != 0) {
        return get_id3v2_tag(tags, key);
    }
    tags = BASS_ChannelGetTags(handle, BASS_TAG_ID3);
    if (tags != 0) {
        return get_id3_tag(tags, key);
    }
    tags = BASS_ChannelGetTags(handle, BASS_TAG_OGG);
    if (tags != 0) {
        return get_ogg_tag(tags, key);
    }
    tags = BASS_ChannelGetTags(handle, BASS_TAG_APE);
    if (tags != 0) {
        return get_ogg_tag(tags, key);
    }
    tags = BASS_ChannelGetTags(handle, BASS_TAG_MUSIC_NAME);
    if (tags != 0 && key == "title") {
        return tags;
    }
    return "";
}

string BassSource::metadata(string key) const
{
    string value = (key == "codec_type") ?
        pimpl->codec_type() :
        get_tag(pimpl->channel, key);
    boost::trim(value);
    return value;
}

string BassSource::name() const
{
    return "Bass Source";
}

float BassSource::loopiness() const
{
    if (!is_module() || pimpl->file_name.size() == 0) {
        return 0;
    }
    // what I'm doing here is find the last 50 ms of a track and return the average positive value
    // i got a bit lazy here, but this part isn't so crucial anyways
    // flags to make decoding as fast as possible. still use 44100 hz, because lower setting might
    // remove upper frequencies that could indicate a loop
    DWORD flags = BASS_MUSIC_DECODE | BASS_SAMPLE_MONO | BASS_MUSIC_NONINTER | BASS_MUSIC_PRESCAN;
    DWORD samplerate = 44100;
    HMUSIC channel = BASS_MusicLoad(FALSE, pimpl->file_name.c_str(), 0, 0 , flags, samplerate);
    size_t check_frames = samplerate / 20;
    size_t check_bytes = check_frames * sizeof(int16_t);

    QWORD length = BASS_ChannelGetLength(channel, BASS_POS_BYTE);
    if (!BASS_ChannelSetPosition(channel, length - check_bytes, BASS_POS_BYTE)) {
        return 0;
    }

    AlignedBuffer<int16_t> buff(check_frames);
    buff.zero();

    DWORD read_bytes = 0;
    int16_t* out = buff.get();
    while (read_bytes < check_bytes && BASS_ErrorGetCode() == BASS_OK) {
        DWORD r = BASS_ChannelGetData(channel, out, check_bytes -  read_bytes);
        out += r * sizeof(int16_t);
        read_bytes += r;
    }
    BASS_MusicFree(channel);

    uint32_t accu = 0;
    out = buff.get();
    for (size_t i = check_frames; i; --i) {
        accu += fabs(*out++);
    }

    return static_cast<float>(accu) / check_frames / -numeric_limits<int16_t>::min();
}

