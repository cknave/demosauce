/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <cstdlib>
#include <string>
#include <sstream>
#include <iostream>

#include <boost/make_shared.hpp>

#include "libreplaygain/replay_gain.h"

#ifdef ENABLE_BASS
    #include "basssource.h"
#endif

#include "avsource.h"
#include "convert.h"

#define MAX_LENGTH 3600     // abort scan if track is too long, in seconds
#define SAMPLERATE 44100

using std::endl;
using std::cout;
using std::string;
using std::stringstream;

using boost::shared_ptr;
using boost::make_shared;
using boost::static_pointer_cast;

void fill_buffer(shared_ptr<Machine>& machine, AudioStream& stream, uint32_t frames)
{
    machine->process(stream, frames);
    if (stream.frames() < frames && !stream.end_of_stream)
    {
        AudioStream helper;
        while (stream.frames() < frames && !stream.end_of_stream)
        {
            machine->process(helper, frames - stream.frames());
            stream.append(helper);
        }
        stream.end_of_stream = helper.end_of_stream;
    }
}

void exit_error(string message)
{
    cout << message << endl;
    exit(EXIT_FAILURE);
}

string scan_song(string file_name, bool do_scan)
{
    bool bass_loaded = false;
    bool av_loaded = false;
    float bitrate = 0;
    shared_ptr<Decoder> decoder;

#ifdef ENABLE_BASS
    shared_ptr<BassSource> bass_decoder = make_shared<BassSource>();
    bass_loaded = bass_decoder->load(file_name, true);
    bitrate = bass_decoder->bitrate();
    decoder = static_pointer_cast<Decoder>(bass_decoder);
#endif

    if (!bass_loaded)
    {
        shared_ptr<AvSource> av_decoder = make_shared<AvSource>();
        av_loaded = av_decoder->load(file_name);
        bitrate = av_decoder->bitrate();
        decoder = static_pointer_cast<Decoder>(av_decoder);
    }

    if (!av_loaded && !bass_loaded)
    {
        exit_error("unknown format");
    }

    uint32_t chan = decoder->channels();
    uint32_t srate = decoder->samplerate();

    if (srate == 0)
    {
        exit_error("samplerate is zero");
    }

    if (chan < 1 || chan > 2)
    {
        exit_error("unsupported number of channels");
    }

    shared_ptr<Machine> source = static_pointer_cast<Machine>(decoder);
    if (srate != SAMPLERATE)
    {
        shared_ptr<Resample> resample = make_shared<Resample>();
        resample->set_rates(srate, SAMPLERATE);
        resample->set_source(decoder);
        source = static_pointer_cast<Machine>(resample);
    }

    uint64_t frames = 0;
    AudioStream stream;
    RG_SampleFormat format = {SAMPLERATE, RG_FLOAT_32_BIT, chan, FALSE};
    RG_Context* context = RG_NewContext(&format);

    if (do_scan || av_loaded)
    {
        while (!stream.end_of_stream)
        {
            fill_buffer(source, stream, 48000);
            float* buffers[2] = {stream.buffer(0), chan == 2 ? stream.buffer(1) : 0};
            // there is a strange bug in the replaygain code that can cause it to report the wrong
            // value if the input buffer has an odd lengh, until the root of the cause is found,
            // this will have to do :(
            uint32_t analyze_frames = stream.frames() - stream.frames() % 2;
            if (do_scan)
            {
                RG_Analyze(context, buffers, analyze_frames);
            }
            frames += stream.frames();
            if (frames > MAX_LENGTH * SAMPLERATE)
            {
                exit_error("too long");
            }
        }
    }

    stringstream msg;

    string artist = decoder->metadata("artist");
    if (!artist.empty())
    {
        msg << "artist:" << artist << endl;
    }

    string title = decoder->metadata("title");
    if (!artist.empty())
    {
        msg << "title:" << title << endl;
    }

    msg << "type:" << decoder->metadata("codec_type") << endl;

    double duration = av_loaded ?
        static_cast<double>(frames) / SAMPLERATE :
        static_cast<double>(decoder->length()) / srate;
    msg << "length:" << duration << endl;

    if (do_scan)
    {
        msg << "replaygain:" << RG_GetTitleGain(context) << endl;
    }
    RG_FreeContext(context);

#ifdef ENABLE_BASS
    if (bass_decoder->is_module())
    {
        msg << "loopiness:" << bass_decoder->loopiness() << endl;
    }
    else
#endif
    {
        msg << "bitrate:" << bitrate  << endl;
        msg << "samplerate:" << decoder->samplerate() << endl;
    }

    return msg.str();
}

int main(int argc, char* argv[])
{
    if (argc < 2 || (*argv[1] == '-' && argc < 3))
    {
        cout << "demosauce scan tool 0.3.2\nsyntax: scan [--no-replaygain] file" << endl;
        return EXIT_FAILURE;
    }

    string file_name = argv[argc - 1];
    bool do_replay_gain = string(argv[1]) != "--no-replaygain";
    cout << scan_song(file_name, do_replay_gain);

    return EXIT_SUCCESS;
}
