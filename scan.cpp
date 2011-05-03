#include <cstdlib>
#include <string>
#include <iostream>

#include <boost/make_shared.hpp>

#include "libreplaygain/replay_gain.h"

#include "logror.h"
#ifdef ENABLE_BASS
    #include "basssource.h"
#endif
#include "avsource.h"
#include "convert.h"

// abort scan if track is too long, in seconds
#define MAX_LENGTH 3600
// samplerate shouldn't matter, but fish had some problems with short samples
#define SAMPLERATE 44100

using std::string;

void fill_buffer(boost::shared_ptr<Machine>& machine, AudioStream& stream, uint32_t const frames)
{
    machine->process(stream, frames);
    if (stream.frames() < frames && !stream.end_of_stream)
    {
        AudioStream helper;
        while (stream.frames() < frames && !stream.end_of_stream)
        {
            machine->process(helper, frames - stream.frames());
            stream.append(helper);
            stream.end_of_stream = helper.end_of_stream;
        }
    }
}

string scan_song(string file_name, bool do_scan)
{
    bool bass_loaded = false;
    bool av_loaded = false;

    boost::shared_ptr<AbstractSource> source;
    boost::shared_ptr<AvSource> av_source = boost::make_shared<AvSource>();
#ifdef ENABLE_BASS
    boost::shared_ptr<BassSource> bass_source = boost::make_shared<BassSource>();
    bass_loaded = bass_source->load(file_name, true);
    if (bass_loaded)
        source = boost::static_pointer_cast<AbstractSource>(bass_source);
#endif

    if (!bass_loaded)
        av_loaded = av_source->load(file_name);
    if (av_loaded)
        source = boost::static_pointer_cast<AbstractSource>(av_source);

    if (!av_loaded && !bass_loaded)
        FATAL("unknown format");

    uint32_t chan = source->channels();
    uint32_t srate = source->samplerate();

    if (srate == 0)
        FATAL("samplerate is zero");
    if (chan < 1 || chan > 2)
        FATAL("unsupported number of channels");

    boost::shared_ptr<Machine> decoder = boost::static_pointer_cast<Machine>(source);
    if (srate != SAMPLERATE)
    {
        LOG_DEBUG("resampling from %1% to %2%"), srate, SAMPLERATE;
        boost::shared_ptr<Resample> resample = boost::make_shared<Resample>();
        resample->set_rates(srate, SAMPLERATE);
        resample->set_source(decoder);
        decoder = boost::static_pointer_cast<Machine>(resample);
    }

    uint64_t const max_frames = MAX_LENGTH * SAMPLERATE;
    uint64_t frames = 0;
    AudioStream stream;
    RG_SampleFormat format = {SAMPLERATE, RG_FLOAT_32_BIT, chan, FALSE};
    RG_Context* context = RG_NewContext(&format);

    if (do_scan || av_loaded)
        while (!stream.end_of_stream)
        {
            fill_buffer(decoder, stream, 48000);
            float* buffers[2] = {stream.buffer(0), chan == 2 ? stream.buffer(1) : 0};
            // there is some bug in the replaygain code that causes it to report the wrong
            // value if the buffer has an odd lengh, until the root of the cause is found,
            // this will have to do :(
            uint32_t analyze_frames = stream.frames() - stream.frames() % 2;
            if (do_scan)
                RG_Analyze(context, buffers, analyze_frames);
            frames += stream.frames();
            if (frames > max_frames)
                FATAL("too long (more than %1% seconds)"), MAX_LENGTH;
        }

    double replay_gain = RG_GetTitleGain(context);
    RG_FreeContext(context);

    // prepare output
    string msg = "type:%1%\nlength:%2%\n";
    if (do_scan)
        msg.append("replaygain:%3%\n");
    double duration = av_loaded ? static_cast<double>(frames) / SAMPLERATE :
        static_cast<double>(source->length()) / srate;
#ifdef BASS_ENABLED
    if (bass_source->is_module())
        msg.append("loopiness:%6%");
    else
        msg.append("bitrate:%4%\nsamplerate:%5%");
    float bitrate = av_loaded ? av_source->bitrate() : bass_source->bitrate();
    float loopiness = bass_source->loopiness()
#else
    msg.append("bitrate:%4%\nsamplerate:%5%");
    float bitrate = av_source->bitrate();
    float loopiness = 0;
#endif
    boost::format formater(msg);
    formater.exceptions(boost::io::no_error_bits);
    string output = str(formater % source->metadata("codec_type") % duration % replay_gain
        % bitrate % srate % loopiness);

    return output;
}

int main(int argc, char* argv[])
{
    log_set_console_level(logror::fatal);
    if (argc < 2 || (*argv[1] == '-' && argc < 3))
    {
        std::cout << "demosauce scan tool 0.3.1\nsyntax: scan [--no-replaygain] file\n";
        return EXIT_FAILURE;
    }

    string file_name = argv[argc - 1];
    bool do_replay_gain = strcmp(argv[1], "--no-replaygain");
    std::cout << scan_song(file_name, do_replay_gain) << std::endl;

    return EXIT_SUCCESS;
}
