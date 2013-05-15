/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXIII by maep
*/

#include <string.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <lame/lame.h>
#include <shout/shout.h>
#include "settings.h"
#include "effects.h"
#include "ffdecoder.h"
#ifdef ENABLE_BASS
    #include "bassdecoder.h"
#endif
#include "cast.h"

#define BUFFER_SIZE 200 // miliseconds
#define LOAD_TRIES  3

static lame_t           lame;
static shout_t*         shout;
static struct stream    stream0;
static struct stream    stream1;
static struct buffer    config;
static struct buffer    mp3buf;
static struct info      info;
static struct decoder   decoder;
static struct fx_fade   fader;
static struct fx_mix    mixer;
static void*            resampler;             
static float            gain;
static long             remaining_frames;
static bool             mixer_enabled;
static bool             fader_enabled;
static sig_atomic_t     decoder_ready;

static void get_next_song(void)
{
    if (settings_debug_song) {
        buffer_resize(&config, strlen(settings_debug_song) + 1);
        strcpy(config.data, settings_debug_song);
    } else {
        buffer_zero(&config);
        int socket = socket_open(settings_demovibes_host, settings_demovibes_port);
        if (socket < 0) {
            LOG_ERROR("[cast] can't connect to demosauce");
            return;
        }
        socket_read(socket, &config);
        socket_close(socket);
    }
}

static void zero_generator(void* dummy, struct stream* s, int frames)
{
    s->channels = settings_encoder_channels;
    s->frames = frames;
    s->end_of_stream = false;
    stream_resize(s, frames);
    stream_zero(s, 0, frames);
}

static void configure_effects(float forced_length)
{
    // play length
    remaining_frames = LONG_MAX;
    if (forced_length > 0) {
        remaining_frames = settings_encoder_samplerate * forced_length;
        LOG_DEBUG("[cast] song length forced to %f seconds", forced_length);
    }

    // resampler
    fx_resample_free(resampler);
    resampler = NULL;
    if (info.samplerate != settings_encoder_samplerate) {
        resampler = fx_resample_init(info.channels, info.samplerate, settings_encoder_samplerate);
        LOG_DEBUG("[cast] resampling from %d to %d Hz", info.samplerate, settings_encoder_samplerate);
    }

    // channel mixing
    char mix_str[8] = {0};
    keyval_str(mix_str, 8, config.data, "mix", "auto");
    mixer_enabled = (settings_encoder_channels == 2) && 
        (strcmp(mix_str, "auto") || (info.flags & INFO_AMIGAMOD));
    if (mixer_enabled) {
        float ratio = keyval_real(config.data, "mix", 0.4);
        ratio = MAX(MIN(ratio, 1.0), 0.0);
        fx_mix_init(&mixer, 1.0 - ratio, ratio, 1.0 - ratio, ratio);
        LOG_DEBUG("[cast] mixing channels with %f ratio", ratio);
    }

    // gain
    gain = keyval_real(config.data, "gain", 0.0);
    LOG_DEBUG("[cast] setting gain to %f dB", gain);
    gain = db_to_amp(gain);

    // fade out
    fader_enabled = keyval_bool(config.data, "fade_out", false); 
    if (fader_enabled) {
        float length = forced_length > 0 ? forced_length : (info.frames / info.samplerate);
        long start = (length - 5) * settings_encoder_samplerate;
        long end = length * settings_encoder_samplerate;
        fx_fade_init(&fader, start, end, 1, 0);
        LOG_DEBUG("[cast] fading out at %f seconds", length);
    }
}

static void update_metadata(void)
{
    char cast_title[1024]   = {0};
    char artist[512]        = {0};
    char title[512]         = {0};

    keyval_str(title, sizeof(title), config.data, "title", settings_error_title);
    keyval_str(artist, sizeof(artist), config.data, "artist", "");

    // remove '-' in artist name, players use it for artist-title separation
    for (size_t i = 0; i < strlen(artist); i++)
        if (cast_title[i] == '-')
            cast_title[i] = ' ';

    size_t len = strlen(artist);
    if (len > 0) {
        strcpy(cast_title, artist);
        strcpy(cast_title + len, " - ");
        len += 3;
    }
    strcpy(cast_title + len, title);
    LOG_DEBUG("[cast] updating metadata to '%s'", cast_title);

    shout_metadata_t* metadata = shout_metadata_new();
    shout_metadata_add(metadata, "song", cast_title);
    if (shout_set_metadata(shout, metadata) != SHOUTERR_SUCCESS)
        LOG_WARN("[cast] metadat update failed (%s)", shout_get_error(shout));
    shout_metadata_free(metadata);
}

static void* load_next(void* data)
{
    static char path[4096];
    float       forced_length   = 0;
    int         tries           = 0;
    bool        loaded          = false;

    if (decoder.free)
        decoder.free(decoder);
    memset(&decoder, 0, sizeof(struct decoder));
    memset(&info, 0, sizeof(struct info));
    
    while (tries++ < LOAD_TRIES && !loaded) {
        get_next_song();
        keyval_str(path, sizeof(path), config.data, "path", "");
        
        if (!util_isfile(path)) {
            LOG_ERROR("[cast] file doesn't exist: '%s'", path);
            continue;
        }
        forced_length = keyval_real(config.data, "length", 0);
#ifdef ENABLE_BASS
        loaded = bass_load(&decoder, path, config.data, settings_encoder_samplerate);
#endif
        if (!loaded)
            loaded = ff_load(&decoder, path);

        if (loaded) {
            decoder.info(&decoder, &info);
#ifdef ENABLE_BASS
            if (forced_length > info.frames / info.samplerate) 
                bass_set_loop_duration(&decoder, forced_length);
#endif
        } else {
            LOG_ERROR("[cast] can't load '%s'", path);
            sleep(3);
        }
    }

    if (loaded && info.frames == 0)
        LOG_WARN("[cast] no length '%s'", path);

    if (!loaded) {
        LOG_WARN("[cast] load failed three times, sending one minute sound of silence");
        decoder.decode  = zero_generator;
        info.samplerate = settings_encoder_samplerate;
        info.channels   = settings_encoder_channels;
        forced_length   = 60;
        buffer_zero(&config);
    }

    configure_effects(forced_length);
    update_metadata();
    decoder_ready = true;
    return NULL;
}

static void cast_init(void)
{
    shout_init();
    shout = shout_new();
    lame = lame_init();
    lame_set_quality(lame, 2);
    lame_set_brate(lame, settings_encoder_bitrate);
    lame_set_num_channels(lame, settings_encoder_channels);
    lame_set_in_samplerate(lame, settings_encoder_samplerate);
    lame_init_params(lame);
    buffer_resize(&mp3buf, BUFFER_SIZE * settings_encoder_bitrate / CHAR_BIT * 4);
    stream1.channels = settings_encoder_channels;
}

static void cast_free(void)
{
    shout_free(shout);
    lame_close(lame);
}

static struct stream* process(int frames)
{
    struct stream* s = &stream0;
    decoder.decode(decoder, &stream0, frames);
    if (resampler) {
        fx_resample(resampler, &stream0, &stream1);
        s = &stream1;
    }
    if (mixer_enabled)
        fx_mix(&mixer, s);
    fx_map(s, settings_encoder_channels);
    fx_gain(s, gain);
    if (fader_enabled)
        fx_fade(&fader, s);
    return s;
}

static bool cast_connect(void)
{
    char bitrate[8]     = {0};
    char samplerate[8]  = {0};
    char channels[4]    = {0};
    snprintf(bitrate, sizeof(bitrate), "%d", settings_encoder_bitrate);
    snprintf(samplerate, sizeof(samplerate), "%d", settings_encoder_samplerate);
    snprintf(channels, sizeof(channels), "%d", settings_encoder_channels); 

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
    shout_set_audio_info(shout, SHOUT_AI_SAMPLERATE, samplerate);
    shout_set_audio_info(shout, SHOUT_AI_CHANNELS, channels);

    // start
    int err = shout_open(shout);
    if (err != SHOUTERR_SUCCESS) 
        LOG_ERROR("[cast] can't connect to icecast (%s)", shout_get_error(shout));
    else
        LOG_INFO("[cast] connected to icecast");
    return err == SHOUTERR_SUCCESS;
}

static void run_encoder(void)
{
    int decode_frames = (settings_encoder_samplerate * BUFFER_SIZE) / 1000;

    while (true) {
        struct stream* s = &stream1;
        if (!decoder_ready) {
            if (s->max_frames < decode_frames)
                stream_resize(s, decode_frames);
            stream_zero(s, 0, decode_frames);
        } else {
            s = process(decode_frames);
            remaining_frames -= s->frames;
            if (s->end_of_stream || remaining_frames < 0) { 
                LOG_DEBUG("[cast] end of stream");
                decoder_ready = false;
                pthread_t thread = {0};
                pthread_create(&thread, NULL, load_next, NULL);
            }
        }
        int siz = lame_encode_buffer_ieee_float(lame, s->buffer[0], s->buffer[1], s->frames, mp3buf.data, mp3buf.size);
        if (siz < 0) { 
           LOG_ERROR("[cast] lame error (%d)", siz);
           return;
        }
        shout_sync(shout);
        int err = shout_send(shout, mp3buf.data, siz);
        if (err != SHOUTERR_SUCCESS) {
            LOG_ERROR("[cast] disconnect (%s)", shout_get_error(shout));
            return;
        }
    }
}

void cast_run(void)
{
    bool load_1st = true;
    while (true) {
        cast_init();
        if (cast_connect()) {
            if (load_1st) {
                load_next(NULL);
                load_1st = false;
            }
            run_encoder();
        }
        cast_free();
        sleep(15); 
    }
}

