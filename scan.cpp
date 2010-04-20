	#include <cstdlib>
#include <string>
#include <iostream>

#include "libreplaygain/replay_gain.h"

#include "logror.h"
#include "basssource.h"
#include "avsource.h"


std::string ScanSong(std::string fileName, bool doReplayGain)
{
	BassSource bassSource;
	AvSource avSource;
	bool bassLoaded = bassSource.Load(fileName, true);
	bool avLoaded = bassLoaded ? false : avSource.Load(fileName);

	uint32_t channels = 0;
	std::string type;
	double length = 0;
	uint32_t samplerate = 0;
	uint32_t bitrate = 0;
	Machine* decoder = 0;

	if (bassLoaded)
	{
		channels = bassSource.Channels();
		type = bassSource.CodecType();
		length = bassSource.Duration();
		samplerate = bassSource.Samplerate();
		bitrate = bassSource.Bitrate();
		decoder = static_cast<Machine*>(&bassSource);
	}

	if (avLoaded)
	{
		channels = avSource.Channels();
		type = avSource.CodecType();
		samplerate = avSource.Samplerate();
		bitrate = avSource.Bitrate();
		decoder = static_cast<Machine*>(&avSource);
	}

	if (!avLoaded && !bassLoaded)
		logror::Fatal("unknown format");
	if (samplerate == 0)
		logror::Fatal("samplerate is zero");
	if (channels < 1 || channels > 2)
		logror::Fatal("unsupported number of channels");

	uint64_t frameCounter = 0;
	RG_SampleFormat format = {samplerate, RG_FLOAT_32_BIT, channels, FALSE};
	RG_Context* context = RG_NewContext(&format);
	AudioStream stream;

	if (doReplayGain || avLoaded)
		while (!stream.endOfStream)
		{
			decoder->Process(stream, 48000);
			float* buffers[2] = {stream.Buffer(0), channels == 2 ? stream.Buffer(1) : NULL};
			if (doReplayGain)
				RG_Analyze(context, buffers, stream.Frames());
			frameCounter += stream.Frames();
		}

	double replayGain = RG_GetTitleGain(context);
	RG_FreeContext(context);
		
	if (avLoaded)
		length = static_cast<double>(frameCounter) / samplerate;
	std::string msg = "type:%1%\nlength:%2%\n";
	if (doReplayGain)
		msg.append("replay gain:%3%\n");
	if (bassSource.IsModule())
		msg.append("loopiness:%6%");
	else
		msg.append("bitrate:%4%\nsamplerate:%5%");

	boost::format formater(msg);
	formater.exceptions(boost::io::no_error_bits);
	return str(formater % type % length % replayGain % bitrate
		% samplerate % bassSource.Loopiness());
}

int main(int argc, char* argv[])
{
	logror::LogSetConsoleLevel(logror::fatal);
	if (argc < 2 || (*argv[1] == '-' && argc < 3))
		logror::Fatal("not enough arguments");

	std::string fileName = argv[argc - 1];
	bool doReplayGain = strcmp(argv[1], "--no-replaygain");
	std::cout << ScanSong(fileName, doReplayGain) << std::endl;

	return EXIT_SUCCESS;
}
