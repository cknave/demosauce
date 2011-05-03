#include <boost/version.hpp>
#if (BOOST_VERSION / 100) < 1036
#error "need at leats BOOST version 1.36"
#endif

#include <cstring>
#include <limits>
#include <algorithm>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include "bass/bass.h"
#include "bass/bass_aac.h"
#include "bass/bassflac.h"

#include "logror.h"
#include "keyvalue.h"
#include "convert.h"
#include "basssource.h"

using namespace std;
using namespace boost;
using namespace boost::filesystem;

#ifdef BASSSOURCE_16BIT_DECODING
	typedef int16_t sample_t;
	#define FLOAT_FLAG 0
#else
	typedef float sample_t;
	#define FLOAT_FLAG BASS_SAMPLE_FLOAT
#endif

struct BassSource::Pimpl
{
	void free();
	std::string codec_type() const;
	ConvertFromInterleaved<sample_t> converter;
	string file_name;
	DWORD channel;
	BASS_CHANNELINFO channelInfo;
	uint32_t samplerate;
	uint64_t currentFrame;
	uint64_t lastFrame;
//	uint64_t length;
};

BassSource::BassSource():
	pimpl(new Pimpl)
{
	pimpl->samplerate = 44100;
	pimpl->channel = 0;
	pimpl->free();
	BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 0);
	if (!BASS_Init(0, 44100, 0, 0, NULL) && BASS_ErrorGetCode() != BASS_ERROR_ALREADY)
		FATAL("BASS init failed (%1%)"), BASS_ErrorGetCode();
}

BassSource::~BassSource()
{
	pimpl->free();
}

bool BassSource::load(std::string file_name)
{
	bool loaded = load(file_name, false);

    static DWORD const amiga_flags = BASS_MUSIC_NONINTER | BASS_MUSIC_PT1MOD;
	if (is_amiga_module())
		BASS_ChannelFlags(pimpl->channel, amiga_flags, amiga_flags);
	else if (is_module())
		BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_RAMP, BASS_MUSIC_RAMP);

    return loaded;
}

bool BassSource::load(std::string file_name, std::string playback_settings)
{
    bool loaded = load(file_name, false);
    if (!is_module())
    {
        return loaded;
    }

    // interpolation; values are audo, auto, off, linear, sinc
    string inter_str = get_value(playback_settings, "bass_inter", "auto");
    if ((is_amiga_module() && inter_str == "auto") || inter_str == "off")
    {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_NONINTER, BASS_MUSIC_NONINTER);
    }
    else if (inter_str == "sinc")
    {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_SINCINTER, BASS_MUSIC_SINCINTER);
    }

    // ramping
    string ramp_str = get_value(playback_settings, "bass_ramp", "auto");
    if ((!is_amiga_module() && ramp_str == "auto") || inter_str == "normal")
    {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_RAMP, BASS_MUSIC_RAMP);
    }
    else if (ramp_str == "sensitive")
    {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_RAMPS, BASS_MUSIC_RAMPS);
    }

    // mode
    string mode_str = get_value(playback_settings, "bass_mode", "auto");
    if ((is_amiga_module() && mode_str == "auto") || mode_str == "pt1")
    {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_PT1MOD, BASS_MUSIC_PT1MOD);
    }
    else if (mode_str == "ft2")
    {
        BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_FT2MOD, BASS_MUSIC_FT2MOD);
    }

    return loaded;
}

bool BassSource::load(string file_name, bool prescan)
{
	pimpl->free();
	pimpl->file_name = file_name;
	DWORD& channel = pimpl->channel;
	DWORD stream_flags = BASS_STREAM_DECODE | (prescan ? BASS_STREAM_PRESCAN : 0) | FLOAT_FLAG;
	DWORD music_flags = BASS_MUSIC_DECODE | BASS_MUSIC_PRESCAN | FLOAT_FLAG;

	LOG_INFO("basssource loading %1%"), file_name;

	// brute force attempt, don't rely on extensions
	channel = BASS_StreamCreateFile(FALSE, file_name.c_str(), 0, 0, stream_flags);
	if (!channel)
		channel = BASS_MusicLoad(FALSE, file_name.c_str(), 0, 0 , music_flags, pimpl->samplerate);
	if (!channel)
		channel = BASS_AAC_StreamCreateFile(FALSE, file_name.c_str(), 0, 0, stream_flags);
	if (!channel)
		channel = BASS_MP4_StreamCreateFile(FALSE, file_name.c_str(), 0, 0, stream_flags);
	if (!channel)
		channel = BASS_FLAC_StreamCreateFile(FALSE, file_name.c_str(), 0, 0, stream_flags);
	if (!channel)
	{
		ERROR("failed to load %1%"), file_name;
		return false;
	}

	BASS_ChannelGetInfo(channel, &pimpl->channelInfo);
	const QWORD length = BASS_ChannelGetLength(channel, BASS_POS_BYTE);
    pimpl->lastFrame = length / (sizeof(sample_t) *  channels());

	if (length == static_cast<QWORD>(-1))
	{
		ERROR("failed to determine duration of %1%"), file_name;
		pimpl->free();
		return false;
	}

	return true;
}

void BassSource::Pimpl::free()
{
	if (channel != 0)
	{
		if (channelInfo.ctype & BASS_CTYPE_MUSIC_MOD) // is module
			BASS_MusicFree(channel);
		else
			BASS_StreamFree(channel);
		if (BASS_ErrorGetCode() != BASS_OK)
			LOG_WARNING("failed to free BASS channel (%1%)"), BASS_ErrorGetCode();
		channel = 0;
	}
	file_name.clear();
	currentFrame = 0;
 	lastFrame = 0;
	memset(&channelInfo, 0, sizeof(BASS_CHANNELINFO));
}

void BassSource::process(AudioStream& stream, uint32_t const frames)
{
	uint32_t const chan = channels();
	if (chan != 2 && chan != 1)
	{
		ERROR("usupported number of channels");
		stream.end_of_stream = true;
		stream.set_frames(0);
		return;
	}

	uint32_t const framesToRead = unsigned_min<uint32_t>(frames, pimpl->lastFrame - pimpl->currentFrame);
	if (framesToRead == 0)
	{
		stream.end_of_stream = true;
		stream.set_frames(0);
		return;
	}

	DWORD const bytesToRead = frames_in_bytes<sample_t>(framesToRead, chan);
	sample_t* const readBuffer = pimpl->converter.input_buffer(frames, chan);
	DWORD const bytesRead = BASS_ChannelGetData(pimpl->channel, readBuffer, bytesToRead);

	if (bytesRead == static_cast<DWORD>(-1) && BASS_ErrorGetCode() != BASS_ERROR_ENDED)
		ERROR("failed to read from channel (%1%)"), BASS_ErrorGetCode();

	uint32_t framesRead = 0;
	if (bytesRead != static_cast<DWORD>(-1))
		framesRead = bytes_in_frames<uint32_t, sample_t>(bytesRead, chan);

	// converter will set stream size
	pimpl->converter.process(stream, framesRead);
	pimpl->currentFrame += framesRead;

	stream.end_of_stream = framesRead != framesToRead || pimpl->currentFrame >= pimpl->lastFrame;
	if(stream.end_of_stream)
		LOG_DEBUG("eos bass %1% frames left"), stream.frames();
}

void BassSource::seek(uint64_t position)
{
}

void BassSource::set_samplerate(uint32_t samplerate)
{
	pimpl->samplerate = samplerate;
}

void BassSource::set_loop_duration(double duration)
{
	if (pimpl->channel == 0)
		return;
    pimpl->lastFrame = numeric_cast<uint64_t>(duration * samplerate());
	BASS_ChannelFlags(pimpl->channel, BASS_SAMPLE_LOOP, BASS_SAMPLE_LOOP);
}

bool BassSource::is_module() const
{
	return pimpl->channelInfo.ctype & BASS_CTYPE_MUSIC_MOD;
}

bool BassSource::is_amiga_module() const
{
	return pimpl->channelInfo.ctype == BASS_CTYPE_MUSIC_MOD;
}

uint32_t BassSource::channels() const
{
	return static_cast<uint32_t>(pimpl->channelInfo.chans);
}

uint32_t BassSource::samplerate() const
{
	return static_cast<uint32_t>(pimpl->channelInfo.freq);
}

float BassSource::bitrate() const
{
	if (is_module())
		return 0;
	double size = BASS_StreamGetFilePosition(pimpl->channel, BASS_FILEPOS_END);
	double duration = static_cast<double>(length()) / samplerate();
	double bitrate = size / (125 * duration);
	return bitrate;
}

uint64_t BassSource::length() const
{
    return pimpl->lastFrame;
}

bool BassSource::seekable() const
{
	return false;
}

bool BassSource::probe_name(string const file_name)
{
	path file(file_name);
	string name = file.filename();

	static size_t const elements = 18;
	char const * ext[elements] = {".mp3", ".ogg", ".m4a", ".flac", ".acc", ".mp4", ".mp2", ".mp1",
		".wav", ".aiff", ".xm", ".mod", ".s3m", ".it", ".mtm", ".umx", ".mo3", ".fst"};
	for (size_t i = 0; i < elements; ++i)
		if (iends_with(name, ext[i]))
			return true;

	// extrawurst for AMP :D
	static size_t const elements_amp = 8;
	char const * ext_amp[elements_amp] = {"xm.", "mod.", "s3m.", "it.", "mtm.", "umx.", "mo3.", "fst."};
	for (size_t i = 0; i < elements_amp; ++i)
		if (istarts_with(name, ext_amp[i]))
			return true;

	return false;
}

std::string BassSource::Pimpl::codec_type() const
{
	switch (channelInfo.ctype)
	{
		case BASS_CTYPE_STREAM_OGG: return "vorbis";
		case BASS_CTYPE_STREAM_MP1: return "mp1";
		case BASS_CTYPE_STREAM_MP2: return "mp2";
		case BASS_CTYPE_STREAM_MP3: return "mp3";
		case BASS_CTYPE_STREAM_AIFF:
		case BASS_CTYPE_STREAM_WAV_PCM:
		case BASS_CTYPE_STREAM_WAV_FLOAT:
		case BASS_CTYPE_STREAM_WAV: return "pcm";
		case BASS_CTYPE_STREAM_MP4: return "mp4";
		case BASS_CTYPE_STREAM_AAC: return "aac";
		case BASS_CTYPE_STREAM_FLAC_OGG:
		case BASS_CTYPE_STREAM_FLAC: return "flac";

		case BASS_CTYPE_MUSIC_MOD: return "mod";
		case BASS_CTYPE_MUSIC_MTM: return "mtm";
		case BASS_CTYPE_MUSIC_S3M: return "s3m";
		case BASS_CTYPE_MUSIC_XM: return "xm";
		case BASS_CTYPE_MUSIC_IT: return "it";
		case BASS_CTYPE_MUSIC_MO3: return "mo3";
		default:;
	}
	return "-";
}

std::string BassSource::metadata(std::string key) const
{
	if (key == "codec_type")
		return pimpl->codec_type();
	// todo: artist / title
	return "";
}

std::string  BassSource::name() const
{
	return "Bass Source";
}

float BassSource::loopiness() const
{
	if (!is_module() || pimpl->file_name.size() == 0)
		return 0;
	// what I'm doing here is find the last 50 ms of a track and return the average positive value
	// i got a bit lazy here, but this part isn't so crucial anyways
	// flags to make decoding as fast as possible. still use 44100 hz, because lower setting might
	// remove upper frequencies that could indicate a loop
	DWORD flags = BASS_MUSIC_DECODE | BASS_SAMPLE_MONO | BASS_MUSIC_NONINTER | BASS_MUSIC_PRESCAN;
	DWORD samplerate = 44100;
	HMUSIC channel = BASS_MusicLoad(FALSE, pimpl->file_name.c_str(), 0, 0 , flags, samplerate);
    size_t check_frames = samplerate / 20;
    size_t check_bytes = check_frames * sizeof(int16_t);

    QWORD length = BASS_ChannelGetLength(channel, BASS_POS_BYTE);
    if (!BASS_ChannelSetPosition(channel, length - check_bytes, BASS_POS_BYTE))
        return 0;

    AlignedBuffer<int16_t> buff(check_frames);
    buff.zero();

    DWORD read_bytes = 0;
    int16_t* out = buff.get();
    while (read_bytes < check_bytes && BASS_ErrorGetCode() == BASS_OK)
    {
    	DWORD r = BASS_ChannelGetData(channel, out, check_bytes -  read_bytes);
        out += r * sizeof(int16_t);
        read_bytes += r;
    }
    BASS_MusicFree(channel);

	uint32_t accu = 0;
    out = buff.get();
	for (size_t i = check_frames; i; --i)
		accu += fabs(*out++);

	return static_cast<float>(accu) / check_frames / -numeric_limits<int16_t>::min();
}
