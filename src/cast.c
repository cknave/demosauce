/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>
#include <lame/lame.h>
#include <unicode/ucnv.h>
#include <unicode/unorm2.h>
#include <shout/shout.h>
#include "settings.h"
#include "effects.h"
#include "ffdecoder.h"
#ifdef ENABLE_BASS
    #include "avdecoder.h"
#endif
#include "cast.h"

#define LOAD_TRIES 3

static lame_t           lame;
static shout_t*         shout;
static struct stream    stream;
static struct info      info;
static void*            decoder;
static struct fx_fade   fader;
static struct fx_mix    mixer;
static void*            resampler;             
static float            gain;
static long             remaining_frames;
static bool             connected;
static bool             mixer_enabled;
static bool             fader_enabled;

static void get_next_song(struct buffer* buffer)
{
    if (settings_debug_song) {
        size_t len = strlen(settings_debug_song);
        if (buffer->size < len + 1)
            buffer_resize(buffer, len + 1);
        strcpy(buffer->data, settings_debug_song);
    } else {
        int socket = socket_open(settings_cast_host, settings_cast_port);
        if (!socket) {
            LOG_ERROR("[cast] open connection to demosauce");
            return;
        }
        socket_read(socket, buffer);
        socket_close(socket);
    }
}

static void zero_generator(void* dummy, struct stream* s, int frames)
{
    if (s->channels != settings_encoder_channels)
        stream_set_channels(s, settings_encoder_channels);
    if (s->max_frames < frames)
        stream_resize(s, frames);
    s->frames = frames;
    stream_zero(s, 0, frames);
}

static void configure_effects(struct buffer* settings, float forced_length)
{
    char mix_str[8] = {0};
    
    remaining_frames = LONG_MAX;
    if (forced_length > 0) {
        remaining_frames = settings_encoder_samplerate * forced_length;
        LOG_DEBUG("[cast] song length forced to %f seconds", forced_length);
    }

    if (info.samplerate != settings_encoder_samplerate) {
        resampler = fx_resample_init(info.channels, info.samplerate, settings_encoder_samplerate);
        LOG_DEBUG("[cast] resampling from %d to %d Hz", info.samplerate, settings_encoder_samplerate);
    }

    keyval_str(mix_str, 8, settings->data, "mix", "auto");
    mixer_enabled = settings_encoder_channels == 2 && (strcmp(mix_str, "auto") || info.amiga_mod);
    if (mixer_enabled) {
        float ratio = keyval_real(settings->data, "mix", 0.4);
        ratio = MAX(MIN(ratio, 1.0), 0.0);
        fx_mix_init(&mixer, 1.0 - ratio, ratio, 1.0 - ratio, ratio);
        LOG_DEBUG("[cast] mixing channels with %f ratio", ratio);
    }

    gain = keyval_real(settings->data, "gain", 0.0);
    LOG_DEBUG("[cast] setting gain to %f dB", gain);
    gain = db_to_amp(gain);

    fader_enabled = keyval_bool(settings->data, "fade_out", false); 
    if (fader_enabled) {
        float length = forced_length > 0 ?  forced_length : info.length;
        long start = (length - 5) * settings_encoder_samplerate;
        long end = length * settings_encoder_samplerate;
        fx_fade_init(&fader, start, end, 1, 0);
        LOG_DEBUG("[update_machines] fading out at %f seconds", length);
    }
}

static void* load_next(void* data)
{
    static struct buffer buffer;
    char* path = NULL; 
    float forced_length = 0;
    int tries = 0;

    if (decoder && info.free)
        info.free(decoder);
    memset(&info, 0, sizeof(struct info));
    
    while (tries++ < LOAD_TRIES && !decoder) {
        path[0] = 0;
        get_next_song(&buffer);
        path = keyval_str(NULL, 0, buffer.data, "path", NULL);
        
        if (!util_isfile(path)) {
            LOG_ERROR("[cast] file doesn't exist: %s", path);
            continue;
        }
        forced_length = keyval_real(buffer.data, "length", 0);
#ifdef ENABLE_BASS
        if ((decoder = bass_load(path, buffer.data))) {
            bass_info(decoder, &info);
            if (forced_length > info.length) 
                bass_set_loop_duration(decoder, forced_length);
        }
#endif
        if (!decoder && (decoder = ff_load(path))) {
            ff_info(decoder, &info);
        }
        
        if (!decoder) {
            LOG_ERROR("[cast] can't load %s", path);
            sleep(3);
        }
    }

    if (decoder && info.length == 0)
        LOG_WARN("[cast] no length %s", path);

    if (!decoder) {
        LOG_WARN("[cast] load failed three times, sending one minute sound of silence");
        info.decode     = zero_generator;
        info.samplerate = settings_encoder_samplerate;
        info.channels   = settings_encoder_channels;
        forced_length   = 60;
    }
    configure_effects(&buffer, forced_length);
    return NULL;
}

static void cast_init(void)
{
    shout_init();
    shout = shout_new();
    lame = lame_init();
    lame_set_quality(lame, 2);
    lame_set_in_samplerate(lame, settings_encoder_bitrate);
}

static void cast_free(void)
{
    shout_free(shout);
    lame_close(lame);
}

static void process(struct stream* stream, int frames)
{
}

static void cast_connect(void)
{
    char bitrate[8]     = {0};
    char samplerate[8]  = {0};
    char channels[4]    = {0};
    snprintf(bitrate, 8, "%d", settings_encoder_bitrate);
    snprintf(samplerate, 8, "%d" settings_encoder_samplerate);
    snprintf(channels, 4, "%d", settings_encoder_channels); 

    // setup connection
    shout_set_host(shout, settings_cast_host);
    shout_set_port(shout, settings_cast_port);
    shout_set_user(shout, settings_cast_user);
    shout_set_password(shout, settings_cast_password);
    shout_set_format(shout, SHOUT_FORMAT_MP3);
    shout_set_mount(shout, settings_cast_mount);
    shout_set_public(shout, 1);
    shout_set_name(shout, settings_cast_name);
    shout_set_url(shout, settings_cast_url);
    shout_set_genre(shout, settings_cast_genre);
    shout_set_description(shout, settings_cast_description);
    shout_set_audio_info(shout, SHOUT_AI_BITRATE, bitrate);
    shout_set_audio_info(cast, SHOUT_AI_SAMPLERATE, samplerate);
    shout_set_audio_info(cast, SHOUT_AI_CHANNELS, channels);

    // start
    if (shout_open(cast) != SHOUTERR_SUCCESS) 
        LOG_ERROR("[cast] can't connect to icecast (%s)", shout_get_error(cast));
    else
        LOG_INFO("[cast] connected to icecast");
}

void cast_run(void)
{
    static unsigned char mp3buf[2048];
    int decode_frames = (settings_encoder_samplerate * settings_decode_buffer_size) / 1000;
    load_next(NULL);

    while (true) {
        int err = 0;
        cast_connect();
        do {
            if (!decoder) {
                stream_zero(&stream, 0, decode_frames);
                continue;
            } else {
                process(&stream, decode_frames);
                remaining_frames -= stream.frames;
                if (stream.end_of_stream || remaining_frames < 0) { 
                    LOG_DEBUG("[cast] end of stream");
                    decoder = NULL;
                    pthread_t thread = {0};
                    pthread_create(&thread, NULL, load_next, NULL);
                }
            }
            // TODO: check stereo 
            int siz = lame_encode_buffer_ieee_float(lame, stream.buffer[0], stream.buffer[1], stream.frames, mp3buf, sizeof(mp3buf)); 
            shout_sync(shout);
            err = shout_send(shout, mp3buf, siz);
        } while (err == SHOUTERR_SUCCESS);
        LOG_ERROR("[cast] icecast disconnected (%s)", shout_get_error(shout));
        sleep(10); 
    }
}

// unicode decompposition
string utf8_to_ascii(string utf8_str)
{
    // BLAST! fromUTF8 requires ics 4.2
    // UnicodeString in_str = UnicodeString::fromUTF8(utf8_str);
    UErrorCode status = U_ZERO_ERROR;
    UConverter* converter = ucnv_open("UTF-8", &status);
    UnicodeString in_str(utf8_str.c_str(), utf8_str.size(), converter, status);
    ucnv_close(converter);

    if (U_FAILURE(status)) {
        LOG_WARN("[utf8_to_ascii] conversion failed (%s)", u_errorName(status));
        return "";
    }

    // convert to ascii as best as possible. it's really smart
    UnicodeString norm_str;
    Normalizer::normalize(in_str, UNORM_NFKD, 0, norm_str, status);

    if (U_FAILURE(status)) {
        LOG_WARN("[utf8_to_ascii] decomposition failed (%s)", u_errorName(status));
        return "";
    }

    // NFKD may produce non ascii chars, these are dropped
    string out_str;
    for (int32_t i = 0; i < norm_str.length(); ++i) {
        if (norm_str[i] >= ' ' && norm_str[i] <= '~') {
            out_str.push_back(static_cast<char>(norm_str[i]));
        }
    }
    return out_str;
}

string create_cast_title(string artist, string title)
{
    // can't use utf-8 metadata in the stream, at least not with bass
    // so unicode decomposition is as a workaround all dashes are removed from artist, because its
    // used as artist-title separator. talk about bad semantics...
    string cast_title = utf8_to_ascii(artist);
    for (size_t i = 0; i < cast_title.size(); ++i) 
        if (cast_title[i] == '-') 
            cast_title[i] = ' ';
    if (cast_title.size() > 0) 
        cast_title.append(" - ");
    cast_title.append(utf8_to_ascii(title));
    LOG_DEBUG("[create_cast_title] '%s', '%s' -> '%s'", cstr(artist), cstr(title), cstr(cast_title));
    return cast_title;
}

void ShoutCastPimpl::update_metadata(SongInfo& song)
{
    string title = get_value(song.settings, "title", settings_error_title);
    string artist = get_value(song.settings, "artist", "");
    string cast_title = create_cast_title(artist, title);

    shout_metadata_t* metadata = shout_metadata_new();
    shout_metadata_add(metadata, "song", cast_title.c_str());
    int err = shout_set_metadata(cast, metadata);
    if (err != SHOUTERR_SUCCESS) 
        LOG_WARN("[update_metadata] error (%d)", err);
    
    shout_metadata_free(metadata);
}

