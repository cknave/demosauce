#include <cstring>
#include <vector>

#include <boost/version.hpp>
#if (BOOST_VERSION / 100) < 1036
    #error "need at least BOOST version 1.36"
#endif

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/format.hpp>
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

#include "globals.h"
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
using boost::scoped_ptr;
using boost::make_shared;
using boost::lexical_cast;
using boost::numeric_cast;

namespace bp = ::boost::process;
namespace fs = ::boost::filesystem;

/*  current processing stack layout, machines in () may be disabled depending on input
    NoiseSource / BassSource / AVCodecSource -> (Resample) -> (MixChannels) ->
    -> MapChannels -> Gain -> [LADSPA planned] -> (LinearFade) -> ShoutCast
*/

typedef int16_t sample_t; // sample type that is fed to encoder

struct ShoutCastPimpl
{
    ShoutCastPimpl();
    virtual ~ShoutCastPimpl();
    void run_encoder();
    void connect();
    void disconnect();
    void writer();
    void reader();
    void change_song();
    void get_next_song(SongInfo& songInfo);
    void init_machines();

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
    int                         change_counter;
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
    change_counter(0)
{
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
    zeroSource = make_shared<ZeroSource>();
#ifdef ENABLE_BASS
    bassSource = make_shared<BassSource>();
#endif
    avSource = make_shared<AvSource>();
    resample = make_shared<Resample>();
    mixChannels = make_shared<MixChannels>();
    mapChannels = make_shared<MapChannels>();
    linearFade = make_shared<LinearFade>();
    gain = make_shared<Gain>();

    float ratio = .4; // default mix ration 0 = no mixing .5 full mixing
    mixChannels->set_mix(1 - ratio, ratio, 1 - ratio, ratio);
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
    pimpl->change_song();
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
            //~ if (change_counter < 0)
            //~ {
                //~ change_counter += frames;
            //~ }
            //~ if (change_counter > 0 || frames != decode_frames) // implicates end of stream
            if (frames != decode_frames) // implicates end of stream
            {
                LOG_DEBUG("end of stream");
                decode_buffer.zero_end((decode_frames - frames) * channels);
                boost::thread thread(bind(&ShoutCastPimpl::change_song, this));
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
            ERROR("icecast connection dropped, trying to reconnect");
            disconnect();
        }
    }
}

//{ // unicode decompposition

string utf8_to_ascii(string const& utf8_str)
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

string create_cast_title(string const& artist, string const& title)
{
    // can't use utf-8 metadata in the stream, at least not with bass
    // so unicode decomposition is as a workaround
    // all dashes are removed from artist, because its
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

string get_random_file(string directoryName)
{
    // I don't think this is a good idea, enumerating >30k files won't be fast
    if (!fs::is_directory(directoryName))
        return "";
    fs::path dir(directoryName);
    uint32_t numFiles = std::distance(dir.begin(), dir.end());
    uint32_t randIndex = rand() * numFiles / RAND_MAX;
    fs::directory_iterator it(dir);
    std::advance(it, randIndex);
    return it->path().string();
}

void ShoutCastPimpl::get_next_song(SongInfo& songInfo)
{
    if (setting::debug_file.size() > 0)
    {
        songInfo.fileName = setting::debug_file;
    }
    else
    {
        sockets.GetSong(songInfo);
    }

    if (songInfo.fileName.size() == 0) // failed to obtain file
    {
        LOG_ERROR("got empty file name");

        if (setting::error_tune.size() > 0)
        {
            LOG_INFO("using error_tune: %1%"), setting::error_tune;
            songInfo.fileName = setting::error_tune;
        }
        else if (setting::error_fallback_dir.size() > 0)
        {
            LOG_INFO("using random file from: %1%"), setting::error_fallback_dir;
            songInfo.fileName = get_random_file(setting::error_fallback_dir);
        }
    }

    if (songInfo.artist.size() == 0 && songInfo.title.size() == 0)
    {
        songInfo.title = setting::error_title;
    }
}

//}

// this is called whenever the song is changed
void ShoutCastPimpl::change_song()
{
    LOG_DEBUG("change_song");
    change_counter = 0;
    decoder_ready = false;
    // reset routing
    resample->set_enabled(false);
    mixChannels->set_enabled(false);
    linearFade->set_enabled(false);

    SongInfo songInfo;
    uint32_t samplerate = setting::encoder_samplerate;
    bool loaded = false;
    int loadTries = 0;

    while (loadTries < 3 && !loaded)
    {
        get_next_song(songInfo);

        if (fs::exists(songInfo.fileName))
        {
#ifdef ENABLE_BASS
            if (!loaded && bassSource->load(songInfo.fileName))
            {
                loaded = true;
                machineStack->add(bassSource, 0);
                samplerate = bassSource->samplerate();
                if (bassSource->is_amiga_module() && setting::encoder_channels == 2)
                {
                    mixChannels->set_enabled(true);
                }
                if (songInfo.loopDuration > 0)
                {
                    bassSource->set_loop_duration(songInfo.loopDuration);
                    uint64_t const start = (songInfo.loopDuration - 5) * setting::encoder_samplerate;
                    uint64_t const end = songInfo.loopDuration * setting::encoder_samplerate;
                    linearFade->set_fade(start, end, 1, 0);
                    linearFade->set_enabled(true);
                }
            }
#endif
            if (!loaded && avSource->load(songInfo.fileName))
            {
                loaded = true;
                machineStack->add(avSource, 0);
                samplerate = avSource->samplerate();
            }
        }
        else
        {
            ERROR("file doesn't exist: %1%"), songInfo.fileName;
        }
        loadTries++;
    }

    if (loadTries >= 3 && !loaded)
    {
        LOG_WARNING("loading failed three times, pumping out a minute of zeros");
        change_counter = -60 * setting::encoder_samplerate;
        machineStack->add(zeroSource, 0);
    }

    if (samplerate != setting::encoder_samplerate)
    {
        resample->set_rates(samplerate, setting::encoder_samplerate);
        resample->set_enabled(true);
    }
    gain->set_amp(db_to_amp(songInfo.gain));
    machineStack->update_routing();

    string title = create_cast_title(songInfo.artist, songInfo.title);
    // TODO: set title
    decoder_ready = true;
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
        ERROR("the encoder process quit");
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

ShoutCast::~ShoutCast() // this HAS to be here, or scoped_ptr will poop in it's pants, header won't work.
{}
