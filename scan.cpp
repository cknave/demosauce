#include <cstdlib>
#include <string>
#include <iostream>

#include "libreplaygain/replay_gain.h"

#include "logror.h"
#include "basssource.h"
#include "avsource.h"

// abort scan if track is too long, in seconds
#define MAX_LENGTH 3600

std::string scan_song(std::string file_name, bool do_scan)
{
	BassSource bass_source;
	AvSource av_source;
	bool bass_loaded = bass_source.load(file_name, true);
	bool av_loaded = bass_loaded ? false : av_source.load(file_name);

	AbstractSource* source = 0;
	if (bass_loaded)
		source = static_cast<AbstractSource*>(&bass_source);
	if (av_loaded)
		source = static_cast<AbstractSource*>(&av_source);

	uint32_t chan = source->channels();
	uint32_t samplerate = source->samplerate();
	
	if (!av_loaded && !bass_loaded)
		FATAL("unknown format");
	if (samplerate == 0)
		FATAL("samplerate is zero");
	if (chan < 1 || chan > 2)
		FATAL("unsupported number of channels");
    
    uint64_t const max_frames = MAX_LENGTH * samplerate;
	uint64_t frames = 0;
	AudioStream stream;
	RG_SampleFormat format = {samplerate, RG_FLOAT_32_BIT, chan, FALSE};
	RG_Context* context = RG_NewContext(&format);

	if (do_scan || av_loaded)
		while (!stream.end_of_stream)
		{
			source->process(stream, 48000);
			float* buffers[2] = {stream.buffer(0), chan == 2 ? stream.buffer(1) : 0};
			if (do_scan)
				RG_Analyze(context, buffers, stream.frames());
			frames += stream.frames();
            if (frames > max_frames)
                FATAL("too long (more than %1% seconds)"), MAX_LENGTH;
		}

	double replay_gain = RG_GetTitleGain(context);
	RG_FreeContext(context);
		
	std::string msg = "type:%1%\nlength:%2%\n";
	if (do_scan)
		msg.append("replaygain:%3%\n");
	if (bass_source.is_module())
		msg.append("loopiness:%6%");
	else
		msg.append("bitrate:%4%\nsamplerate:%5%");
	
	double duration = static_cast<double>(av_loaded ? frames : source->length()) / samplerate; 
	float bitrate = av_loaded ? av_source.bitrate() : bass_source.bitrate();
	
	boost::format formater(msg);
	formater.exceptions(boost::io::no_error_bits);
	return str(formater % source->metadata("codec_type") % duration % replay_gain 
		% bitrate % samplerate % bass_source.loopiness());
}

int main(int argc, char* argv[])
{
	log_set_console_level(logror::fatal);
	if (argc < 2 || (*argv[1] == '-' && argc < 3))
	{
		std::cout << "demosauce scan tool 0.2\nsyntax: scan [--no-replaygain] file\n";
		return EXIT_FAILURE;
	}

	std::string file_name = argv[argc - 1];
	bool do_replay_gain = strcmp(argv[1], "--no-replaygain");
	std::cout << scan_song(file_name, do_replay_gain) << std::endl;

	return EXIT_SUCCESS;
}
