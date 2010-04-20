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
#include "convert.h"
#include "basssource.h"

using namespace std;
using namespace boost;
using namespace boost::filesystem;
using namespace logror;

#ifdef BASSSOURCE_16BIT_DECODING
	typedef int16_t sample_t;
	#define FLOAT_FLAG 0
#else
	typedef float sample_t;
	#define FLOAT_FLAG BASS_SAMPLE_FLOAT
#endif

struct BassSource::Pimpl
{
	void Free();
	ConvertFromInterleaved<sample_t> converter;
	string fileName;
	DWORD channel;
	BASS_CHANNELINFO channelInfo;
	double duration;
	uint32_t samplerate;
	uint64_t currentFrame;
	uint64_t lastFrame;
};

BassSource::BassSource():
	pimpl(new Pimpl)
{
	pimpl->samplerate = 44100;
	pimpl->channel = 0;
	pimpl->Free();
	BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 0);
	if (!BASS_Init(0, 44100, 0, 0, NULL) &&
		BASS_ErrorGetCode() != BASS_ERROR_ALREADY)
		Fatal("BASS init failed (%1%)"), BASS_ErrorGetCode();
}

BassSource::~BassSource()
{
	pimpl->Free();
}

bool BassSource::Load(string fileName, bool prescan)
{
	pimpl->Free();
	pimpl->fileName = fileName;
	DWORD & channel = pimpl->channel;
	DWORD stream_flags = BASS_STREAM_DECODE | (prescan ? BASS_STREAM_PRESCAN : 0) | FLOAT_FLAG;
	DWORD music_flags = BASS_MUSIC_DECODE | BASS_MUSIC_PRESCAN | FLOAT_FLAG;

	Log(info, "basssource loading %1%"), fileName;

	// brute force attempt, don't rely on extensions
	channel = BASS_StreamCreateFile(FALSE, fileName.c_str(), 0, 0, stream_flags);
	if (!channel)
		channel = BASS_MusicLoad(FALSE, fileName.c_str(), 0, 0 , music_flags, pimpl->samplerate);
	if (!channel)
		channel = BASS_AAC_StreamCreateFile(FALSE, fileName.c_str(), 0, 0, stream_flags);
	if (!channel)
		channel = BASS_MP4_StreamCreateFile(FALSE, fileName.c_str(), 0, 0, stream_flags);
	if (!channel)
		channel = BASS_FLAC_StreamCreateFile(FALSE, fileName.c_str(), 0, 0, stream_flags);
	if (!channel)
	{
		Error("failed to load %1%"), fileName;
		return false;
	}

	BASS_ChannelGetInfo(channel, &pimpl->channelInfo);
	const QWORD length = BASS_ChannelGetLength(channel, BASS_POS_BYTE);
	pimpl->duration = BASS_ChannelBytes2Seconds(channel, length);

	if (length == static_cast<QWORD>(-1))
	{
		Error("failed to determine duration of %1%"), fileName;
		pimpl->Free();
		return false;
	}

	static DWORD const amiga_flags = BASS_MUSIC_NONINTER | BASS_MUSIC_PT1MOD;
	if (IsAmigaModule())
		BASS_ChannelFlags(pimpl->channel, amiga_flags, amiga_flags);
	else
		BASS_ChannelFlags(pimpl->channel, BASS_MUSIC_RAMP, BASS_MUSIC_RAMP);

	return true;
}

void BassSource::Pimpl::Free()
{
	if (channel != 0)
	{
		if (channelInfo.ctype & BASS_CTYPE_MUSIC_MOD) // is module
			BASS_MusicFree(channel);
		else
			BASS_StreamFree(channel);
		if (BASS_ErrorGetCode() != BASS_OK)
			Log(warning, "failed to free BASS channel (%1%)"), BASS_ErrorGetCode();
		channel = 0;
	}
	fileName.clear();
	duration = 0;
	currentFrame = 0;
 	lastFrame = numeric_limits<uint64_t>::max();
	memset(&channelInfo, 0, sizeof(BASS_CHANNELINFO));
}

void BassSource::Process(AudioStream& stream, uint32_t const frames)
{
	uint32_t const channels = Channels();
	if (channels != 2 && channels != 1)
	{
		Error("usupported number of channels");
		stream.endOfStream = true;
		stream.SetFrames(0);
		return;
	}

	uint32_t const framesToRead = unsigned_min<uint32_t>(frames, pimpl->lastFrame - pimpl->currentFrame);
	if (framesToRead == 0)
	{
		stream.endOfStream = true;
		stream.SetFrames(0);
		return;
	}

	DWORD const bytesToRead = FramesInBytes<sample_t>(framesToRead, channels);
	sample_t* const readBuffer = pimpl->converter.Buffer(frames, channels);
	DWORD const bytesRead = BASS_ChannelGetData(pimpl->channel, readBuffer, bytesToRead);

	if (bytesRead == static_cast<DWORD>(-1) && BASS_ErrorGetCode() != BASS_ERROR_ENDED)
		Error("failed to read from channel (%1%)"), BASS_ErrorGetCode();

	uint32_t framesRead = 0;
	if (bytesRead != static_cast<DWORD>(-1))
		framesRead = BytesInFrames<uint32_t, sample_t>(bytesRead, channels);

	// converter will set stream size
	pimpl->converter.Process(stream, framesRead);
	pimpl->currentFrame += framesRead;
	stream.endOfStream = framesRead != framesToRead || pimpl->currentFrame >= pimpl->lastFrame;
	if(stream.endOfStream) LogDebug("eos bass %1% frames left"), stream.Frames();
}

void BassSource::SetSamplerate(uint32_t moduleSamplerate)
{
	pimpl->samplerate = moduleSamplerate;
}

void BassSource::SetLoopDuration(double duration)
{
	if (pimpl->channel == 0 || duration < 0)
		return;
	pimpl->duration = duration;
	pimpl->lastFrame = numeric_cast<uint64_t>(duration * Samplerate());
	BASS_ChannelFlags(pimpl->channel, BASS_SAMPLE_LOOP, BASS_SAMPLE_LOOP);
}

float BassSource::Loopiness() const
{
	if (!IsModule() || pimpl->fileName.size() == 0)
		return 0;
	// what I'm doing here is find the last 50 ms of a track and return the average positive value
	// i got a bit lazy here, but this part isn't so crucial anyways
	// flags to make decoding as fast as possible. still use 44100 hz, because lower setting might
	// remove upper frequencies that could indicate a loop
	DWORD const flags = BASS_MUSIC_DECODE | BASS_SAMPLE_MONO | BASS_MUSIC_NONINTER;
	static DWORD const sampleRate = 44100;
	static size_t const checkFrames = sampleRate / 20;
	HMUSIC channel = BASS_MusicLoad(FALSE, pimpl->fileName.c_str(), 0, 0 , flags, sampleRate);
	static size_t const buffSize = 44100;
	int16_t* buff = new int16_t[buffSize * 2]; // replace with aligned buffer class
	memset(buff, 0, sizeof(buff)); // just to be save
	int16_t* bufA = buff;
	int16_t* bufB = buff + buffSize;
	DWORD bytes = buffSize * sizeof(int16_t);
	while (bytes == buffSize * sizeof(int16_t)) // um seeking, anyone?
	{
		bytes = BASS_ChannelGetData(channel, bufB, buffSize * sizeof(int16_t));
		if (bytes == static_cast<DWORD>(-1) && BASS_ErrorGetCode() != BASS_ERROR_ENDED)
		{
			Log(warning, "error on loop scan (%1%)"), BASS_ErrorGetCode();
			return 0;
		}
		if (BASS_ErrorGetCode() == BASS_ERROR_ENDED)
			bytes = 0;
		swap(bufA, bufB);
	}
	DWORD const frames = bytes / sizeof(int16_t);
	size_t bufAOffset = frames < checkFrames ? buffSize + frames - checkFrames : buffSize;
	size_t bufBOffset = frames < checkFrames ? 0 : frames - checkFrames;
	uint32_t accu = 0;
	bufA += bufAOffset;
	for (size_t i = bufAOffset; i < buffSize; ++i)
		accu += fabs(*bufA++);
	bufB += bufBOffset;
	for (size_t i = bufBOffset; i < frames; ++i)
		accu += fabs(*bufB++);
	delete[] buff;
	BASS_MusicFree(channel);
	return static_cast<float>(accu) / checkFrames / -numeric_limits<int16_t>::min();
}

bool BassSource::IsModule() const
{
	return pimpl->channelInfo.ctype & BASS_CTYPE_MUSIC_MOD;
}

bool BassSource::IsAmigaModule() const
{
	return pimpl->channelInfo.ctype == BASS_CTYPE_MUSIC_MOD;
}

uint32_t BassSource::Channels() const
{
	return static_cast<uint32_t>(pimpl->channelInfo.chans);
}

uint32_t BassSource::Samplerate() const
{
	return static_cast<uint32_t>(pimpl->channelInfo.freq);
}

uint32_t BassSource::Bitrate() const
{
	DWORD const fileLength = BASS_StreamGetFilePosition(pimpl->channel, BASS_FILEPOS_END);
 	uint32_t const bitrate = static_cast<uint32_t>(fileLength / (125 * Duration()) + 0.5);
	bool gah = IsModule();
	return gah ? 0 : bitrate;
}

double BassSource::Duration() const
{
	return pimpl->duration;
}

bool BassSource::CheckExtension(string const fileName)
{
	path file(fileName);
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

string BassSource::CodecType() const
{
	switch (pimpl->channelInfo.ctype)
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
