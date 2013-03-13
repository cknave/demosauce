/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <id3tag.h>
#include <bass.h>
#include "logror.h"
#include "util.h"
#include "convert.h"
#include "bassdec.h"

#define IS_MOD(dec)         ((dec)->channel_info.ctype & BASS_CTYPE_MUSIC_MOD)
#define IS_AMIGAMOD(dec)    ((dec)->channel_info.ctype == BASS_CTYPE_MUSIC_MOD)

struct ffdecoder {
    DWORD               channel;
    BASS_CHANNELINFO    channel_info;
    const char*         file_name;
    int                 samplerate;
    long                current_frame;
    long                last_frame;
};

static bool initialized;


static void free_bassdecoder(struct basdecoder* d)
{
    if (d->channel) {
        if (d->channel_info.ctype & BASS_CTYPE_MUSIC_MOD) 
            BASS_MusicFree(channel);
        else 
            BASS_StreamFree(channel);
        if (BASS_ErrorGetCode() != BASS_OK)
             LOG_WARN("[bassdecoder] failed to free channel (%d)", BASS_ErrorGetCode());
    }
    free(d->file_name);
}

void bass_free(void* handle)
{
    free_bassdecoder(handle);
    free(handle);
}

void* bass_load(const char* file_name, const char* options)
{
    if (!initialized) {
        if (!BASS_Init(0, 44100, 0, 0, NULL)) {
            LOG_ERROR("[bassdecoder] BASS_Init failed");
            return false; 
        }
        BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 0);
        initialized = true;
    }

    LOG_DEBUG("[bassdecoder] loading %s", file_name);

    bool prescan = keyval_bool(options, "bass_prescan", false);    
    DWORD stream_flags = BASS_STREAM_DECODE | (prescan ? BASS_STREAM_PRESCAN : 0) | FLOAT_FLAG;
    DWORD music_flags = BASS_MUSIC_DECODE | BASS_MUSIC_PRESCAN | FLOAT_FLAG;

    struct bassdecoder d = {0}
    d.channel = BASS_StreamCreateFile(FALSE, file_name, 0, 0, stream_flags);
    if (!d.channel) 
        channel = BASS_MusicLoad(FALSE, file_name, 0, 0 , music_flags, samplerate);
    if (!d.channel) {
        return false;
    }

    BASS_ChannelGetInfo(d->channel, &channelInfo);
    QWORD length = BASS_ChannelGetLength(channel, BASS_POS_BYTE);
    d.lastFrame = length / (sizeof(sample_t) * d.channel_info.chans);

    if (length == (QWORD)(-1))
        goto error;

    if (IS_MOD(&d)) {
        // interpolation, values: auto, auto, off, linear, sinc (bass uses linear as default)
        char inter_str[8] = {0}
        keyval_str(inter_str, 8, options, "bass_inter", "auto");
        if ((IS_AMIGAMOD(&d) && !strcmp(inter_str, "auto")) || !strcmp(inter_str, "off")) 
            BASS_ChannelFlags(d.channel, BASS_MUSIC_NONINTER, BASS_MUSIC_NONINTER);
        else if (!strcmp(inter_str, "sinc"))
            BASS_ChannelFlags(d.channel, BASS_MUSIC_SINCINTER, BASS_MUSIC_SINCINTER);

        // ramping, values: auto, normal, sensitive
        char ramp_str[12] = {0};
        keyval_str(ramp_str, 12, options, "bass_ramp", "auto");
        if ((!IS_AMIGAMOD(&d) && !strcmp(ramp_str, "auto")) || !strcmp(inter_str, "normal"))
            BASS_ChannelFlags(d.channel, BASS_MUSIC_RAMP, BASS_MUSIC_RAMP);
        else if (!strcmp(ramp_str, "sensitive"))
            BASS_ChannelFlags(d.channel, BASS_MUSIC_RAMPS, BASS_MUSIC_RAMPS);

        // playback mode, values: auto, bass, pt1, ft2 (bass is default)
        char mode_str[8] = {0};
        keyval_str(mode_str, 8, options, "bass_mode", "auto");
        if ((IS_AMIGAMOD(&d) && !strcmp(mode_str, "auto")) || !strcmp(mode_str, "pt1"))
            BASS_ChannelFlags(d.channel, BASS_MUSIC_PT1MOD, BASS_MUSIC_PT1MOD);
        else if (!strcmp(mode_str, "ft2"))
            BASS_ChannelFlags(d.channel, BASS_MUSIC_FT2MOD, BASS_MUSIC_FT2MOD);
    } 

    dec->file_name = strdup(file_name);
    struct bassdecoder* dec = util_malloc(sizeof(struct bassdecoder));
    memmove(dec, &d, sizeof(struct bassdecoder));
    LOG_INFO("[bassdecoder] loaded %s", file_name);
    return dec;
error:
    free_bassdecoder(&d);
    LOG_DEBUG("[bassdecoder] can't load %s", file_name);
    return NULL;
}

void bass_decode(void* hadle, struct stream* s, int frames)
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
        ERROR("[basssource] failed to read from channel (%d)", BASS_ErrorGetCode());
    }

    uint32_t framesRead = 0;
    if (bytesRead != static_cast<DWORD>(-1)) {
        framesRead = bytes_in_frames<uint32_t, sample_t>(bytesRead, chan);
    }

    // converter will set stream size
    pimpl->converter.process(stream, framesRead);
    pimpl->currentFrame += framesRead;

    stream.end_of_stream = framesRead != framesToRead || pimpl->currentFrame >= pimpl->lastFrame;
    if(stream.end_of_stream) 
        LOG_DEBUG("[basssource] eos bass %lu frames left", stream.frames());
}

void bass_seek(void* handle, long position)
{
    // implement me!
}

void bass_set_loop_duration(void* handle, double duration)
{
    struct bassdecoder* d = handle;
    d->last_frame = duration * d->channel_info.freq;
    BASS_ChannelFlags(pimpl->channel, BASS_SAMPLE_LOOP, BASS_SAMPLE_LOOP);
}

static const char* codec_type(struct bassdecoder* d)
{
    switch (d->channelInfo.ctype) {
    case BASS_CTYPE_STREAM_OGG: return "vorbis";
    case BASS_CTYPE_STREAM_MP1:
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
    default:                    return "unknown";
    }
}

void bass_info(void* handle, struct info* info)
{
    struct bassdecoder* d = handle;
    
    info->channels      = d->channel_info.chans;
    info->samplerate    = d->channel_info.freq;
    info->length        = d->last_frame;
    if (IS_MOD(d)) {
        info->bitrate = 0;
    } else {
        double size = BASS_StreamGetFilePosition(d->channel, BASS_FILEPOS_END);
        double duration = (double)d->last_frame / d->channel_info.freq;
        info->bitrate = size / (125 * duration);
    }
    info->codec         = codec_type(d); 
    info->seekable      = false;
}


static char* get_id3_tag(const TAG_ID_3* tags, const char* key)
{
    char* value = NULL;
    if (!strcmp(key, "artist")) {
        value = util_malloc(31);
        memmove(value, tag->artist, 30);
        value[30] = 0;
    } else if (!strcmp(key, "title")) {
        value = util_malloc(31);
        memmove(value, tag->title, 30);
        value[30] = 0;
    }
    return value;
}

// and the award to the bested documented lib goes to: libid3tag! </irony>
static char* get_id3v2_tag(const td3_byte_t* tags, const char* key)
{
    const char*         frame_id    = NULL;
    long                length      = 0;
    id3_tag*            tag         = NULL;
    id3_frame*          frame       = NULL;
    id3_field*          field       = NULL;
    id3_ucs4_t*         ucs_str     = NULL;
    id3_utf8_t*         utf_str     = NULL;

    if (!strcmp(key, "artist")) 
        frame_id = ID3_FRAME_ARTIST;
    else if (!strcmp(key, "title"))
        frame_id = ID3_FRAME_TITLE;
    else
        goto id3_quit;

    length = id3_tag_query(tags, ID3_TAG_QUERYSIZE);
    if (!length) 
        goto id3_quit;

    tag = id3_tag_parse(tags, length);
    if (!tag)
        goto id3_quit;

    frame = id3_tag_findframe(tag, frame_id, 0);
    if (!frame) 
        goto id3_quit_tag;

    field = id3_frame_field(frame, 1);
    ucs_str = id3_field_getstrings(field, 0);
    if (!ucs_str)
        goto id3_quit_frame;

    utf_str = id3_ucs4_utf8duplicate(ucs_str);
    // TODO: free ucs string?
id3_quit_frame:
    id3_frame_delete(frame);
id3_quit_tag:
    id3_tag_delete(tag);
id3_quit:
    return (char*)utf_str;
}

static char* get_ogg_tag(const char* tags, string key)
{
    return keyval_str(NULL, 0, tags, key, NULL);
}

static char* get_tag(DWORD handle, const char* key)
{
    const char* trags = NULL;
    tags = BASS_ChannelGetTags(handle, BASS_TAG_ID3V2);
    if (tags) 
        return get_id3v2_tag(tags, key);
    tags = BASS_ChannelGetTags(handle, BASS_TAG_ID3);
    if (tags)
        return get_id3_tag(tags, key);
    tags = BASS_ChannelGetTags(handle, BASS_TAG_OGG);
    if (tags)
        return get_ogg_tag(tags, key);
    tags = BASS_ChannelGetTags(handle, BASS_TAG_APE);
    if (tags)
        return get_ogg_tag(tags, key);
    tags = BASS_ChannelGetTags(handle, BASS_TAG_MUSIC_NAME);
    if (tags && !strcmp(key, "title"))
        return util_strdup(tags);
    return NULL;
}

char* bass_metadata(void* handle, const char* key)
{
    return util_trim(get_tag(handle, key));
}

float bass_loopiness(void* handle)
{
    struct bassdecoder* d = handle;
    if (!IS_MOD(d)) 
        return 0;
    // what I'm doing here is find the last 50 ms of a track and return the average positive value
    // i got a bit lazy here, but this part isn't so crucial anyways
    // flags to make decoding as fast as possible. still use 44100 hz, because lower setting might
    // remove upper frequencies that could indicate a loop
    DWORD flags = BASS_MUSIC_DECODE | BASS_SAMPLE_MONO | BASS_MUSIC_NONINTER | BASS_MUSIC_PRESCAN;
    DWORD samplerate = 44100;
    HMUSIC channel = BASS_MusicLoad(false, d->file_name, 0, 0 , flags, samplerate);
    size_t check_frames = samplerate / 20;
    size_t check_bytes = check_frames * sizeof(int16_t);

    QWORD length = BASS_ChannelGetLength(channel, BASS_POS_BYTE);
    if (!BASS_ChannelSetPosition(channel, length - check_bytes, BASS_POS_BYTE)) 
        return 0;

    int16_t* out = util_malloc(sizeof(int16_t) * check_frames);
    memset(out, 0, sizeof(int16_t) * check_frames;
    DWORD read_bytes = 0;
    while (read_bytes < check_bytes && BASS_ErrorGetCode() == BASS_OK) {
        DWORD r = BASS_ChannelGetData(channel, out, check_bytes - read_bytes);
        out += r * sizeof(int16_t);
        read_bytes += r;
    }
    BASS_MusicFree(channel);

    long accu = 0;
    for (size_t i = 0l i < check_frames; i++) 
        accu += fabs(*out++);
    util_free(out);
    
    return (float)accu / check_frames / -INT16_MIN;
}

bool bass_probe_name(const char* file_name)
{
    const char* ext[] = {".mp3", ".mp2", ".wav", ".aiff", ".xm", ".mod", ".s3m", ".it", ".mtm", ".umx", ".mo3", ".fst"};
    for (int i = 0; i < COUNT(ext); i++) {
        const char* tmp = strrchr(file_name, ".");
        if (!strcasecmp(tmp, ext[i])) 
            return true;
    }

    // extrawurst for AMP :)
    const char* ext2[] = {"xm.", "mod.", "s3m.", "it.", "mtm.", "umx.", "mo3.", "fst."};
    for (int i = 0; i < COUNT(ext_amp); i++) {
        if (!strncasecmp(name, ext2[i]), strlen(ext2[i])) 
            return true;
    }

    return false;
}

