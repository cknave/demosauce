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
#include <unistd.h>
#include <unicode/ucnv.h>
#include <unicode/unistr.h>
#include <unicode/normlzr.h>
#include <shout/shout.h>
#include "settings.h"
#include "sockets.h"
#include "effects.h"
#include "convert.h"
#include "avsource.h"
#ifdef ENABLE_BASS
 #include "basssource.h"
#endif
#include "cast.h"

/*  current processing stack layout, machines in () may be disabled depending on input
    machines in [] may be disabled depending on build configuration
    ZeroSource / [BassSource] / AVCodecSource -> (Resample) -> (MixChannels) ->
    -> MapChannels -> Gain -> [LADSPA] -> (LinearFade) -> ShoutCast
*/

struct song_info {
    const char* file;
    const char* settings;
    int         samplerate;
    double      length;
    double      play_time;
    bool        amiga_mode;
};

static struct stream    stream;
static struct decoder*  decoder;
#ifdef ENABLE_BASS
static struct decoder*  bassdec;
#endif
static struct decoder*  avdec;
static shout_t*         cast;
static bool             encoder_running;
static bool             decoder_ready;
static bool             connected           = true;
static long             remaining_frames;

void cast_init(void)
{
    init_socket();
    shout_init();
    cast = shout_new();
}

ShoutCastPimpl::~ShoutCastPimpl()
{
    shout_free(cast);
}

void cast_run(void)
{
    load_next();
    while (true) {
        run_encoder();
        sleep(10); 
    }
}

void ShoutCastPimpl::writer()
{
    uint32_t const channels = setting::encoder_channels;
    uint32_t const decode_frames = (setting::encoder_samplerate * setting::decode_buffer_size) / 1000;

    // decode buffer size = sizeof(sample_t) * decode_frames * channels
    AlignedBuffer<sample_t> decode_buffer(decode_frames * channels);

    while (encoder_running) {
        // decode and read some
        if (decoder_ready) {
            uint32_t frames = converter.process(decode_buffer.get(), decode_frames, channels);
            remaining_frames += frames;

            if (frames != decode_frames || remaining_frames > 0) { // end of song 
                LOG_DEBUG("[writer] end of stream");
                decode_buffer.zero_end((decode_frames - frames) * channels);
                // load next song in separate thread
                decoder_ready = false;
                boost::thread thread(bind(&ShoutCastPimpl::load_next, this));
            }
        } else {
            decode_buffer.zero();
        }
        shout_sync(cast); // I did syncing in the reader but that ccaused problems
        encoder_input->write(decode_buffer.get_char(), decode_buffer.size_bytes());
    }
}

void ShoutCastPimpl::reader()
{
    AlignedBuffer<char> send_buffer(setting::encoder_samplerate);

    while (encoder_running) {
        while (!connected) {
            boost::this_thread::sleep(boost::posix_time::seconds(10));
            connect();
        }

        // blocks if not enough data is available, dunno what happens when the process quits
        encoder_output->read(send_buffer.get(), send_buffer.size_bytes());
        // shout_sync(cast); syncing moved to writer
        int err = shout_send(cast, send_buffer.get_uchar(), send_buffer.size_bytes());
        if (err != SHOUTERR_SUCCESS) {
            LOG_ERROR("[reader] icecast connection dropped, trying to recover(%s)", shout_get_error(cast));
            disconnect();
        }
    }
}

static void load_next(void)
{
    SongInfo song = {"", "", settings_encoder_samplerate, 0, 0, false};

    int tries = 0;
    bool loaded = false;
    while (tries++ < 3 && !loaded) {
        get_next_song(song);
        song.forced_length = get_value_flt(song.settings, "length", 0.0);

        if (!util_isfile(song.file)) {
            LOG_ERROR("[load_next] file doesn't exist: %s", song.file);
            continue;
        }

#ifdef ENABLE_BASS
        if (!loaded && bass_load(bassdec, song.file, song.settings)) {
            decoder = basssdec;
            if (song.forced_length > 0) 
                bass_set_loop_duration(bassdec, song.forced_length);
            loaded = true;
        }
#endif
        if (!loaded && av_load(avdec, song.file)) {
            decoder = avdec;
            loaded = true;
        }

        song.samplerate = decoder->samplerate;
        song.length = decoder->length / decoder->samplerate;

        if (!loaded) {
            LOG_ERROR("[load_next] can't decode %s", song.file);
            sleep(3);
        }
        
    }

    if (loaded && song.length == 0)
        LOG_WARN("[load_next] no legth %s", song.file);

    if (!loaded) {
        LOG_WARN("[load_next] loading failed three times, sending a bunch of zeros");
        song.forced_length = 60;
    }

    update_machines(song);
    update_metadata(song);

    decoder_ready = true;
}

void get_next_song(struct song_info* song)
{
        if (!settings_debug_song) 
            sockets_next_song(&song.settings);
        else 
            song.settings = settings_debug_song;
        
        song.file = get_value_str(song.settings, "path", "");

        if (song.file.empty()) {
            if (fs::is_regular_file(setting::error_tune)) {
                LOG_WARN("[get_next_song] file name is empty, using error_tune");
                song.file = settings_error_tune;
            } else if (fs::is_directory(setting::error_fallback_dir)) {
                LOG_WARN("[get_next_song] file name is empty, using error_fallback_dir");
                song.file = get_random_file(setting::error_fallback_dir);
            } else {
                LOG_WARN("[get_next_song] file name is empty, and your fallback settings suck");
            }
        }
}

static void process(void)
{

}

void ShoutCastPimpl::update_machines(SongInfo& song)
{
    // reset routing
    resample->set_enabled(false);
    mixChannels->set_enabled(false);
    linearFade->set_enabled(false);

    remaining_frames = std::numeric_limits<int64_t>::min();
    if (song.forced_length > 0) {
        remaining_frames = -numeric_cast<int>(setting::encoder_samplerate * song.forced_length);
        LOG_DEBUG("[update_machines] song length forced to %f seconds", song.forced_length);
    }

    if (get_value(song.settings, "fade_out", false)) {
        double length = song.forced_length > 0 ?
            song.forced_length :
            song.length;
        uint64_t start = numeric_cast<uint64_t>((length - 5) * setting::encoder_samplerate);
        uint64_t end = numeric_cast<uint64_t>(length * setting::encoder_samplerate);
        linearFade->set_fade(start, end, 1, 0);
        linearFade->set_enabled(true);
        LOG_DEBUG("[update_machines] song fading out at %f seconds", length);
    }

    if (song.samplerate != setting::encoder_samplerate) {
        resample->set_rates(song.samplerate, setting::encoder_samplerate);
        resample->set_enabled(true);
        LOG_DEBUG("[update_machines] resampling %u to %u Hz", song.samplerate, setting::encoder_samplerate);
    }

    bool auto_mix = (get_value(song.settings, "mix", "auto") == "auto");
    if (setting::encoder_channels == 2 && (!auto_mix || song.amiga_mode)) {
        double ratio = get_value(song.settings, "mix", 0.4);
        ratio = std::max(ratio, 0.0);
        ratio = std::min(ratio, 1.0);
        mixChannels->set_mix(1.0 - ratio, ratio, 1.0 - ratio, ratio);
        mixChannels->set_enabled(true);
        LOG_DEBUG("[update_machines] mixing channels with %f ratio", ratio);
    }

    double song_gain = get_value(song.settings, "gain", 0.0);
    gain->set_amp(db_to_amp(song_gain));
    LOG_DEBUG("[update_machines] applying gain of %f dB", song_gain);

    machineStack->update_routing();
}

void ShoutCastPimpl::run_encoder()
{
    // split up encoder command into strings for poost::process
    vector<string> args;
    boost::split(args, setting::encoder_command, boost::is_any_of("\t "));
    string exe = args[0];
    args.erase(args.begin());

    if (!fs::exists(exe)) try {
        exe = bp::find_executable_in_path(exe);
    } catch (fs::filesystem_error& e) {
        FATAL("[run_encoder] can't locate encoder executable: %s", cstr(exe));
    }

    bp::context ctx;
    ctx.streams[bp::stdin_id] = bp::behavior::async_pipe();
    ctx.streams[bp::stdout_id] = bp::behavior::async_pipe();
    ctx.streams[bp::stderr_id] = bp::behavior::null();

    LOG_INFO("[run_encoder] starting encoder: %s", cstr(setting::encoder_command));
    try {
        // launch encoder
        bp::child c = bp::create_child(exe, args, ctx);
        encoder_input = boost::in_place(c.get_handle(bp::stdin_id));
        encoder_output = boost::in_place(c.get_handle(bp::stdout_id));
        encoder_running = true;

        // launch reader and writer threads
        boost::thread write_tread(bind(&ShoutCastPimpl::writer, this));
        boost::thread read_tread(bind(&ShoutCastPimpl::reader, this));

        // blocks until encoder quits. the encoder should only quit in abnormal situations
        c.wait();
        encoder_running = false;
        write_tread.interrupt();    // try interrupting any blocking calls
        read_tread.interrupt();     // try interrupting any blocking calls
        LOG_ERROR("[run_encoder] encoder process stopped, trying to recover");
    } catch (fs::filesystem_error& e) {
        FATAL("[run_encoder] failed to launch encoder (%s)", e.what());
    }
}

void ShoutCastPimpl::connect()
{
    // setup connection
    shout_set_host(cast, setting::cast_host.c_str());
    shout_set_port(cast, setting::cast_port);
    shout_set_user(cast, setting::cast_user.c_str());
    shout_set_password(cast, setting::cast_password.c_str());
    shout_set_format(cast, SHOUT_FORMAT_MP3);
    shout_set_mount(cast, setting::cast_mount.c_str());
    shout_set_public(cast, 1);
    shout_set_name(cast, setting::cast_name.c_str());
    shout_set_url(cast, setting::cast_url.c_str());
    shout_set_genre(cast, setting::cast_genre.c_str());
    shout_set_description(cast, setting::cast_description.c_str());

    string bitrate = lexical_cast<string>(setting::encoder_bitrate);
    shout_set_audio_info(cast, SHOUT_AI_BITRATE, bitrate.c_str());

    string samplerate = lexical_cast<string>(setting::encoder_samplerate);
    shout_set_audio_info(cast, SHOUT_AI_SAMPLERATE, samplerate.c_str());

    string channels =  lexical_cast<string>(setting::encoder_channels);
    shout_set_audio_info(cast, SHOUT_AI_CHANNELS, channels.c_str());

    // start
    if (shout_open(cast) != SHOUTERR_SUCCESS) {
        LOG_ERROR("[connect] can't connect to icecast (%s)", shout_get_error(cast));
    } else {
        LOG_INFO("[connect] connected to icecast");
        connected = true;
    }
}

void ShoutCastPimpl::disconnect()
{
    connected = false;
    shout_close(cast);
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
    string title = get_value(song.settings, "title", setting::error_title);
    string artist = get_value(song.settings, "artist", "");
    string cast_title = create_cast_title(artist, title);

    shout_metadata_t* metadata = shout_metadata_new();
    shout_metadata_add(metadata, "song", cast_title.c_str());
    int err = shout_set_metadata(cast, metadata);
    if (err != SHOUTERR_SUCCESS) 
        LOG_WARN("[update_metadata] error (%d)", err);
    
    shout_metadata_free(metadata);
}

