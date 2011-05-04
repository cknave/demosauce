#include <cstring>
#include <csignal>

#include <vector>

#include <boost/version.hpp>
#if (BOOST_VERSION / 100) < 1036
    #error "need at least BOOST version 1.36"
#endif

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/optional/optional.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/utility/in_place_factory.hpp>

// not official yet, maybe never will be
#include "boost/process.hpp"

#include <unicode/ucnv.h>
#include <unicode/unistr.h>
#include <unicode/normlzr.h>

#include <shout/shout.h>

#include "settings.h"
#include "keyvalue.h"
#include "sockets.h"
#include "effects.h"
#include "convert.h"
#include "avsource.h"
#ifdef ENABLE_BASS
    #include "basssource.h"
#endif
#include "shoutcast.h"

// some classes in boost and std namespace collide...
using std::string;
using std::vector;

using boost::bind;
using boost::optional;
using boost::shared_ptr;
using boost::make_shared;
using boost::numeric_cast;
using boost::lexical_cast;

namespace bp = ::boost::process;
namespace fs = ::boost::filesystem;

/*  current processing stack layout, machines in () may be disabled depending on input
    NoiseSource / BassSource / AVCodecSource -> (Resample) -> (MixChannels) ->
    -> MapChannels -> Gain -> [LADSPA planned] -> (LinearFade) -> ShoutCast
*/

typedef int16_t sample_t; // sample type that is fed to encoder

struct SongInfo
{
    string file;
    string settings;
    uint32_t samplerate;
    double length;
    double forced_length;
    bool amiga_mode;
};

struct ShoutCastPimpl
{
    ShoutCastPimpl();
    virtual ~ShoutCastPimpl();

    void init_machines();
    void run_encoder();

    void connect();
    void disconnect();

    void writer();
    void reader();

    void load_next();
    void get_next_song(SongInfo& song);
    void update_machines(SongInfo& song);
    void update_metadata(SongInfo& song);

    //-----------------

    optional<bp::postream>      encoder_input;
    optional<bp::pistream>      encoder_output;

    Sockets                     sockets;
    ConvertToInterleaved        converter;

    shared_ptr<MachineStack>    machineStack;
    shared_ptr<ZeroSource>      zeroSource;
#ifdef ENABLE_BASS
    shared_ptr<BassSource>      bassSource;
#endif
    shared_ptr<AvSource>        avSource;
    shared_ptr<Resample>        resample;
    shared_ptr<MixChannels>     mixChannels;
    shared_ptr<MapChannels>     mapChannels;
    shared_ptr<LinearFade>      linearFade;
    shared_ptr<Gain>            gain;

    shout_t*                    cast;
    bool                        encoder_running;
    bool                        decoder_ready;
    bool                        connected;
    int64_t                     remaining_frames;
};

ShoutCast::ShoutCast() :
    pimpl(new ShoutCastPimpl)
{}

ShoutCastPimpl::ShoutCastPimpl() :
    sockets(setting::demovibes_host, setting::demovibes_port),
    cast(0),
    encoder_running(false),
    decoder_ready(true),
    connected(false),
    remaining_frames(0)
{
    signal(SIGPIPE, SIG_IGN); // otherwise we won't be able to recover
    shout_init();
    cast = shout_new();
    init_machines();
}

ShoutCastPimpl::~ShoutCastPimpl()
{
    shout_free(cast);
}

void ShoutCastPimpl::init_machines()
{
    machineStack = make_shared<MachineStack>();
    zeroSource  = make_shared<ZeroSource>();
#ifdef ENABLE_BASS
    bassSource  = make_shared<BassSource>();
#endif
    avSource    = make_shared<AvSource>();
    resample    = make_shared<Resample>();
    mixChannels = make_shared<MixChannels>();
    mapChannels = make_shared<MapChannels>();
    linearFade  = make_shared<LinearFade>();
    gain        = make_shared<Gain>();

    mapChannels->set_channels(setting::encoder_channels);

    machineStack->add(zeroSource);
    machineStack->add(resample);
    machineStack->add(mixChannels);
    machineStack->add(mapChannels);
    machineStack->add(gain);
    // dsp here
    machineStack->add(linearFade);

    converter.set_source(machineStack);
}

void ShoutCast::Run()
{
    pimpl->connect();
    pimpl->load_next();
    while (true)
    {
        pimpl->run_encoder();
        boost::this_thread::sleep(boost::posix_time::seconds(10));
    }
}

void ShoutCastPimpl::writer()
{
    uint32_t const channels = setting::encoder_channels;
    uint32_t const decode_frames = setting::encoder_samplerate;

    AlignedBuffer<sample_t> decode_buffer(setting::encoder_samplerate * channels);

    while (encoder_running)
    {
        // decode and read some
        if (decoder_ready)
        {
            uint32_t frames = converter.process(decode_buffer.get(), decode_frames, channels);
            remaining_frames += frames;

            if (frames != decode_frames || remaining_frames > 0) // end of song
            {
                LOG_DEBUG("end of stream");
                decode_buffer.zero_end((decode_frames - frames) * channels);
                boost::thread thread(bind(&ShoutCastPimpl::load_next, this));
            }
        }
        else
        {
            decode_buffer.zero();
        }
        // this should block once internal buffers are full
        encoder_input->write(decode_buffer.get_char(), decode_buffer.size_bytes());
    }
}

void ShoutCastPimpl::reader()
{
    AlignedBuffer<char> send_buffer(setting::encoder_samplerate);

    while (encoder_running)
    {
        while (!connected)
        {
            boost::this_thread::sleep(boost::posix_time::seconds(10));
            connect();
        }

        // blocks if not enough data is available, dunno what happens when the process quits
        encoder_output->read(send_buffer.get(), send_buffer.size_bytes());
        shout_sync(cast);
        int err = shout_send(cast, send_buffer.get_uchar(), send_buffer.size_bytes());

        if (err != SHOUTERR_SUCCESS)
        {
            ERROR("icecast connection dropped, trying to recover");
            disconnect();
        }
    }
}

// this is called whenever the song is changed
void ShoutCastPimpl::load_next()
{
    LOG_DEBUG("load_next");
    decoder_ready = false;

    SongInfo song = {"", "", setting::encoder_samplerate, 0, 0, false};

    int loadTries = 0;
    bool loaded = false;
    while (loadTries++ < 3 && !loaded)
    {
        get_next_song(song);
        song.forced_length = get_value(song.settings, "length", 0.0);

        if (!fs::exists(song.file))
        {
            ERROR("file doesn't exist: %1%"), song.file;
            continue;
        }

#ifdef ENABLE_BASS
        if (!loaded && (loaded = bassSource->load(song.file, song.settings)))
        {
            machineStack->add(bassSource, 0);
            if (song.forced_length > 0)
            {
                bassSource->set_loop_duration(song.forced_length);
            }
            song.samplerate = bassSource->samplerate();
            song.length = bassSource->length();
            song.amiga_mode = bassSource->is_amiga_module();
        }
#endif
        if (!loaded && (loaded = avSource->load(song.file)))
        {
            machineStack->add(avSource, 0);
            song.length = avSource->length();
            song.samplerate = avSource->samplerate();
        }
    }

    if (loadTries >= 3 && !loaded)
    {
        LOG_WARNING("loading failed three times, sending a bunch of zeros");
        machineStack->add(zeroSource, 0);
        song.forced_length = 60;
    }

    update_machines(song);
    update_metadata(song);

    decoder_ready = true;
}

string get_random_file(string directoryName)
{
    // I don't think this is a good idea, enumerating >30k files won't be fast
    fs::path dir(directoryName);
    uint32_t numFiles = std::distance(dir.begin(), dir.end());
    uint32_t randIndex = rand() * numFiles / RAND_MAX;
    fs::directory_iterator it(dir);
    std::advance(it, randIndex);
    return it->path().string();
}

void ShoutCastPimpl::get_next_song(SongInfo& song)
{
        if (setting::debug_file.empty())
        {
            song.settings = sockets.get_next_song();
            song.file = get_value(song.settings, "path", "");
        }
        else
        {
            song.file = setting::debug_file;
        }

        if (song.file.empty())
        {
            if (fs::is_regular_file(setting::error_tune))
            {
                LOG_WARNING("file name is empty, using error_tune");
                song.file = setting::error_tune;
            }
            else if (fs::is_directory(setting::error_fallback_dir))
            {
                LOG_WARNING("file name is empty, using error_fallback_dir");
                song.file = get_random_file(setting::error_fallback_dir);
            }
            else
            {
                LOG_WARNING("file name is empty, and your fallback settings suck");
            }
        }
}

void ShoutCastPimpl::update_machines(SongInfo& song)
{
    // reset routing
    resample->set_enabled(false);
    mixChannels->set_enabled(false);
    linearFade->set_enabled(false);

    remaining_frames = std::numeric_limits<int64_t>::min();
    if (song.forced_length > 0)
    {
        remaining_frames = -numeric_cast<int>(setting::encoder_samplerate * song.forced_length);
        LOG_DEBUG("song length forced to %1% seconds"), song.forced_length;
    }

    if (get_value(song.settings, "fade_out", false))
    {
        double length = song.forced_length > 0 ? song.forced_length : song.length;
        uint64_t start = numeric_cast<int>((length  - 5) * setting::encoder_samplerate);
        uint64_t end = numeric_cast<int>(length  * setting::encoder_samplerate);
        linearFade->set_fade(start, end, 1, 0);
        linearFade->set_enabled(true);
        LOG_DEBUG("song fading out at %1% seconds"), length;
    }

    if (song.samplerate != setting::encoder_samplerate)
    {
        resample->set_rates(song.samplerate, setting::encoder_samplerate);
        resample->set_enabled(true);
        LOG_DEBUG("resampling %1% to %2% Hz"), song.samplerate, setting::encoder_samplerate;
    }

    bool auto_mix = (get_value(song.settings, "mix", "auto") == "auto");
    if (setting::encoder_channels == 2 && (!auto_mix || song.amiga_mode))
    {
        double ratio = get_value(song.settings, "mix", 0.4);
        ratio = std::max(ratio, 0.);
        ratio = std::min(ratio, 1.);
        mixChannels->set_mix(1. - ratio, ratio, 1. - ratio, ratio);
        mixChannels->set_enabled(true);
        LOG_DEBUG("mixing channels with %1% ratio"), ratio;
    }

    double song_gain = get_value(song.settings, "gain", 0.0);
    gain->set_amp(db_to_amp(song_gain));
    LOG_DEBUG("applying gain of %1% dB"), song_gain;

    machineStack->update_routing();
}

void ShoutCastPimpl::run_encoder()
{
    // split up encoder command into strings for poost::process
    vector<string> args;
    boost::split(args, setting::encoder_command, boost::is_any_of("\t "));
    string exe = args[0];
    args.erase(args.begin());

    if (!fs::exists(exe)) try
    {
        exe = bp::find_executable_in_path(exe);
    }
    catch (fs::filesystem_error& e)
    {
        FATAL("can't locate encoder: %1%"), exe;
    }

    bp::context ctx;
    ctx.streams[bp::stdin_id] = bp::behavior::async_pipe();
    ctx.streams[bp::stdout_id] = bp::behavior::async_pipe();
    ctx.streams[bp::stderr_id] = bp::behavior::null();

    LOG_INFO("starting encoder: %1%"), setting::encoder_command;
    try
    {
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
        ERROR("encoder process stopped, trying to recover");
    }
    catch (fs::filesystem_error& e)
    {
        FATAL("failed to launch encoder (%1%)"), e.what();
    }
}

void ShoutCastPimpl::connect()
{
    // setup connection
    shout_set_host(cast, setting::cast_host.c_str());
    shout_set_port(cast, setting::cast_port);
    shout_set_user(cast, "source");
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
    int err = shout_open(cast);
    switch (err)
    {
        case SHOUTERR_SUCCESS:
            LOG_INFO("connected to icecast");
            connected = true;
            break;
        case SHOUTERR_CONNECTED:
            FATAL("icecast connection already open");
            break;
        case SHOUTERR_UNSUPPORTED:
        case SHOUTERR_INSANE:
            FATAL("can't set up connection, probably a configuration error");
            break;
        case SHOUTERR_NOLOGIN:
            FATAL("authentification error");
            break;
        case SHOUTERR_MALLOC:
            FATAL("out of memory");
            break;
        default:
            FATAL("unknown error while connecting to icecast");
            break;
        case SHOUTERR_NOCONNECT:
        case SHOUTERR_SOCKET:
            ERROR("network error");
            break;
    }
}

void ShoutCastPimpl::disconnect()
{
    connected = false;
    shout_close(cast);
}

//{ // unicode decompposition
string utf8_to_ascii(string utf8_str)
{
    // BLAST! fromUTF8 requires ics 4.2
    // UnicodeString in_str = UnicodeString::fromUTF8(utf8_str);
    UErrorCode status = U_ZERO_ERROR;
    UConverter* converter = ucnv_open("UTF-8", &status);
    UnicodeString in_str(utf8_str.c_str(), utf8_str.size(), converter, status);
    ucnv_close(converter);

    if (U_FAILURE(status))
    {
        LOG_WARNING("utf8 conversion failed (%1%)"), u_errorName(status);
        return "";
    }

    // convert to ascii as best as possible. it's really smart
    UnicodeString norm_str;
    Normalizer::normalize(in_str, UNORM_NFKD, 0, norm_str, status);

    if (U_FAILURE(status))
    {
        LOG_WARNING("unicode decomposition failed (%1%)"), u_errorName(status);
        return "";
    }

    // NFKD may produce non ascii chars, these are dropped
    string out_str;
    for (int32_t i = 0; i < norm_str.length(); ++i)
        if (norm_str[i] >= ' ' && norm_str[i] <= '~')
            out_str.push_back(static_cast<char>(norm_str[i]));
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
    LOG_DEBUG("unicode decomposition: %1%, %2% -> %3%"), artist, title, cast_title;
    return cast_title;
}
//}

void ShoutCastPimpl::update_metadata(SongInfo& song)
{
    string title = get_value(song.settings, "title", setting::error_title);
    string artist = get_value(song.settings, "artist", "");
    string cast_title = create_cast_title(artist, title);

    shout_metadata_t* metadata = shout_metadata_new();
    shout_metadata_add(metadata, "song", cast_title.c_str());
    int err = shout_set_metadata(cast, metadata);
    if (err != SHOUTERR_SUCCESS)
    {
        LOG_WARNING("shout_set_metadata failed with code %1%"), err;
    }

    shout_metadata_free(metadata);
}

ShoutCast::~ShoutCast() // this HAS to be here, or scoped_ptr will poop in it's pants, header won't work.
{}
