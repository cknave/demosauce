#include <boost/version.hpp>
#if (BOOST_VERSION / 100) < 1036
	#error "need at leats BOOST version 1.36"
#endif

#include <cstring>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread/thread.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include <unicode/ucnv.h>
#include <unicode/unistr.h>
#include <unicode/normlzr.h>

#include "bass/bass.h"
#include "bass/bassenc.h"

#include "globals.h"
#include "sockets.h"
#include "dsp.h"
#include "convert.h"
#include "basssource.h"
#include "avsource.h"
#include "basscast.h"

using namespace std;
using namespace boost;
using namespace logror;
using namespace boost::filesystem;

/*	current processing stack layout
	NoiseSource / BassSource / AVCodecSource -> (Resample) -> (MixChannels) ->
	-> MapChannels -> (LinearFade) -> Gain -> BassCast
*/

typedef int16_t sample_t; // output sample type

struct BassCastPimpl
{
	void Start();
	void ChangeSong();
	void GetNextSong(SongInfo& songInfo);
	void InitMachines();
	//-----------------
	Sockets sockets;
	ConvertToInterleaved converter;
	shared_ptr<MachineStack> machineStack;
	shared_ptr<NoiseSource> noiseSource;
	shared_ptr<BassSource> bassSource;
	shared_ptr<Resample> resample;
	shared_ptr<AvSource> avSource;
	shared_ptr<MixChannels> mixChannels;
	shared_ptr<MapChannels> mapChannels;
	shared_ptr<LinearFade> linearFade;
	shared_ptr<Gain> gain;
	HENCODE encoder;
	HSTREAM sink;
	vector<float> readBuffer;
	//-----------------
	BassCastPimpl() :
		sockets(setting::demovibes_host, setting::demovibes_port),
		encoder(0),
		sink(0)
		{
			BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 0);
			if (!BASS_Init(0, setting::encoder_samplerate, 0, 0, NULL) &&
				BASS_ErrorGetCode() != BASS_ERROR_ALREADY)
				Fatal("BASS init failed (%1%)"), BASS_ErrorGetCode();
			InitMachines();
			ChangeSong();
			Start();
		}
};

BassCast::BassCast() : pimpl(new BassCastPimpl) { }

template <typename T> shared_ptr<T> new_shared()
{
	return shared_ptr<T>(new T);
}

void BassCastPimpl::InitMachines()
{
	machineStack = new_shared<MachineStack>();
	noiseSource = new_shared<NoiseSource>();
	bassSource = new_shared<BassSource>();
	avSource = new_shared<AvSource>();
	resample = new_shared<Resample>();
	mixChannels = new_shared<MixChannels>();
	mapChannels = new_shared<MapChannels>();
	linearFade = new_shared<LinearFade>();
	gain = new_shared<Gain>();

	noiseSource->SetChannels(setting::encoder_channels);
	float ratio = setting::amiga_channel_ratio;
	mixChannels->Set(1 - ratio, ratio, 1 - ratio, ratio);
	mapChannels->SetOutChannels(setting::encoder_channels);

	machineStack->AddMachine(noiseSource);
	machineStack->AddMachine(resample);
	machineStack->AddMachine(mixChannels);
	machineStack->AddMachine(mapChannels);
	machineStack->AddMachine(linearFade);
	machineStack->AddMachine(gain);

	converter.SetSource(machineStack);
}

void BassCast::Run()
{
	AlignedBuffer<char> buff(setting::encoder_samplerate * 2);
	for (;;)
	{
		DWORD bytesRead = BASS_ChannelGetData(pimpl->sink, buff.Get(), buff.Size());
		if (bytesRead == static_cast<DWORD>(-1))
			Fatal("lost sink channel (%1%)"), BASS_ErrorGetCode();
	}
}

string utf8_to_ascii(string const & utf8_str)
{
	// BLAST! fromUTF8 requires ics 4.2
	// UnicodeString in_str = UnicodeString::fromUTF8(utf8_str);
	UErrorCode status = U_ZERO_ERROR;
	UConverter* converter = ucnv_open("UTF-8", &status);
	UnicodeString in_str(utf8_str.c_str(), utf8_str.size(), converter, status);
	ucnv_close(converter);

	if (U_FAILURE(status))
	{
		Log(warning, "utf8 conversion failed (%1%)"), u_errorName(status);
		return "";
	}

	// convert to ascii as best as possible. it's really smart
	UnicodeString norm_str;
	Normalizer::normalize(in_str, UNORM_NFKD, 0, norm_str, status);

	if (U_FAILURE(status))
	{
		Log(warning, "unicode decomposition failed (%1%)"), u_errorName(status);
		return "";
	}

	// NFKD may produce non ascii chars, these are dropped
	string out_str;
	for (int32_t i = 0; i < norm_str.length(); ++i)
		if (norm_str[i] >= ' ' && norm_str[i] <= '~')
			out_str.push_back(static_cast<char>(norm_str[i]));
	return out_str;
}

string create_cast_title(string const & artist, string const & title)
{
	// can't use utf-8 metadata in the stream, at least not with bass
	// so unicode decomposition is as a workaround
	// all dashes are removed from artist, because it's
	// used as artist-title separator. talk about bad semantics...
	string cast_title = utf8_to_ascii(artist);
	for (size_t i = 0; i < cast_title.size(); ++i)
		if (cast_title[i] == '-')
			cast_title[i] = ' ';
	if (cast_title.size() > 0)
        cast_title.append(" - ");
	cast_title.append(utf8_to_ascii(title));
	LogDebug("unicode decomposition: %1%, %2% -> %3%"), artist, title, cast_title; 
	return cast_title;
}

string get_random_file(string directoryName)
{
	if (!is_directory(directoryName))
		return "";
	path dir(directoryName);
	uint32_t numFiles = std::distance(dir.begin(), dir.end());
	uint32_t randIndex = rand() * numFiles / RAND_MAX;
	directory_iterator it(dir);
	std::advance(it, randIndex);
	return it->path().string();
}

void BassCastPimpl::GetNextSong(SongInfo& songInfo)
{
	if (setting::debug_file.size() > 0)
		songInfo.fileName = setting::debug_file;
	else
		sockets.GetSong(songInfo);
	
	if (songInfo.fileName.size() == 0)
    {
        Log(info, "loading random file");
		songInfo.fileName = get_random_file(setting::error_fallback_dir);
    }
	if (songInfo.fileName.size() == 0)
		songInfo.fileName = setting::error_tune;	
	if (songInfo.artist.size() == 0 && songInfo.title.size() == 0)
		songInfo.title = setting::error_title;
}

// this is called whenever the song is changed
void BassCastPimpl::ChangeSong()
{
	// reset routing
	resample->SetEnabled(false);
	mixChannels->SetEnabled(false);
	linearFade->SetEnabled(false);

	SongInfo songInfo;
	uint32_t samplerate = setting::encoder_samplerate;
	double duration = 0;

	for (bool loadSuccess = false; !loadSuccess;)
	{
		bool bassLoaded = false;
		bool avLoaded = false;
		GetNextSong(songInfo);
		
		if (exists(songInfo.fileName))
		{
		// try loading by extension
			if (BassSource::CheckExtension(songInfo.fileName))
				bassLoaded = bassSource->Load(songInfo.fileName);
			else if (AvSource::CheckExtension(songInfo.fileName))
				avLoaded = avSource->Load(songInfo.fileName);

		// if above failed
			if (!bassLoaded && !avLoaded)
				bassLoaded = bassSource->Load(songInfo.fileName);
			if (!bassLoaded && !avLoaded)
				avLoaded = avSource->Load(songInfo.fileName);
		}
		else
			Error("file doesn't exist: %1%"), songInfo.fileName;

		if (bassLoaded)
		{
			machineStack->AddMachine(bassSource, 0);
			samplerate = bassSource->Samplerate();
			duration = bassSource->Duration();
			if (bassSource->IsAmigaModule() && setting::encoder_channels == 2)
				mixChannels->SetEnabled(true);
			if (songInfo.loopDuration > 0)
			{
				bassSource->SetLoopDuration(songInfo.loopDuration);
				uint64_t const start = (songInfo.loopDuration - 5) * setting::encoder_samplerate;
				uint64_t const end = songInfo.loopDuration * setting::encoder_samplerate;
				linearFade->Set(start, end, 1, 0);
				linearFade->SetEnabled(true);
			}
			loadSuccess = true;
		}

 		if (avLoaded)
 		{
 			machineStack->AddMachine(avSource, 0);
 			samplerate = avSource->Samplerate();
 			loadSuccess = true;
 		}

		if (!loadSuccess && songInfo.fileName == setting::error_tune)
		{
			Log(warning, "no error tune, playing some glorious noise"), songInfo.fileName;
			noiseSource->SetDuration(120 * setting::encoder_samplerate);
			machineStack->AddMachine(noiseSource, 0);
			gain->SetAmp(DbToAmp(-24));
			loadSuccess = true;
		}
	}

	Log(info, "duration %1% seconds"), duration;

	if (samplerate != setting::encoder_samplerate)
	{
		resample->Set(samplerate, setting::encoder_samplerate);
		resample->SetEnabled(true);
	}
	// once clipping is working also apply positive gain
	gain->SetAmp(DbToAmp(songInfo.gain));
	machineStack->UpdateRouting();

	string title = create_cast_title(songInfo.artist, songInfo.title);
	BASS_Encode_CastSetTitle(encoder, title.c_str(), NULL);
}

// this is where most of the shit happens
DWORD FillBuffer(HSTREAM handle, void * buffer, DWORD length, void * user)
{
	BassCastPimpl& pimpl = *reinterpret_cast<BassCastPimpl*>(user);
	uint32_t const channels = setting::encoder_channels;
	uint32_t const frames = BytesInFrames<uint32_t, sample_t>(length, channels);
	sample_t* const outBuffer = reinterpret_cast<sample_t*>(buffer);

	uint32_t const procFrames = pimpl.converter.Process(outBuffer, frames, channels);

	if (procFrames != frames) // implicates end of stream
	{
		Log(info, "end of stream");
		size_t bytesRead = FramesInBytes<sample_t>(procFrames, channels);
		memset(reinterpret_cast<char*>(buffer) + bytesRead, 0, length - bytesRead);
		pimpl.ChangeSong();
	}
	return length;
}

// encoder death notification
void EncoderNotify(HENCODE handle, DWORD status, void * user)
{
	BassCastPimpl & pimpl = * reinterpret_cast<BassCastPimpl*>(user);
	if (status >= 0x10000)
		return;
	if (!BASS_Encode_Stop(pimpl.encoder))
		Log(warning, "failed to stop old encoder %1%"), BASS_ErrorGetCode();
	bool deadUplink = status == BASS_ENCODE_NOTIFY_CAST;
	if (deadUplink)
		Error("the server connection died");
	else
		Error("the encoder died");
	this_thread::sleep(deadUplink ? posix_time::seconds(60) : posix_time::seconds(1));
	pimpl.Start();
}

void
BassCastPimpl::Start()
{
	// set up source stream -- samplerate, number channels, flags, callback, user data
	sink = BASS_StreamCreate(setting::encoder_samplerate, setting::encoder_channels,
		BASS_STREAM_DECODE, &FillBuffer, this);
	if (!sink)
		Fatal("couldn't create sink (%1%)"), BASS_ErrorGetCode();
	// setup encoder
	string command = str(format("lame -r -s %1% -b %2% -") % setting::encoder_samplerate % setting::encoder_bitrate);
	Log(info, "starting encoder %1%"), command;
	encoder = BASS_Encode_Start(sink, command.c_str(), BASS_ENCODE_NOHEAD | BASS_ENCODE_AUTOFREE, NULL, 0);
	if (!encoder)
		Fatal("couldn't start encoder (%1%)"), BASS_ErrorGetCode();
	// setup casting
	string host = setting::cast_host;
	ResolveIp(setting::cast_host, host); // if resolve fails, host will retain it's original value
	string server = str(format("%1%:%2%/%3%") % host % setting::cast_port % setting::cast_mount);
	const char * pass = setting::cast_password.c_str();
	const char * content = BASS_ENCODE_TYPE_MP3;
	const char * name = setting::cast_name.c_str();
	const char * url = setting::cast_url.c_str();
	const char * genre = setting::cast_genre.c_str();
	const char * desc = setting::cast_description.c_str();
	const DWORD bitrate = setting::encoder_bitrate;
	//NULL = no additional headers, TRUE = make public ok(list at shoutcast?)
	if (!BASS_Encode_CastInit(encoder, server.c_str(), pass, content, name, url, genre, desc, NULL, bitrate, TRUE))
		Fatal("couldn't set up connection with icecast (%1%)\n\tserver=%2%\n\tpass=%3%\n\tcontent=%4%\n\tname=%5%\n" \
			"\turl=%6%\n\tgenre=%7%\n\tdesc=%8%\n\tbitrate=%9%"), BASS_ErrorGetCode(), server, pass, content, name,
			url,genre,desc, bitrate;
	Log(info, "connected to icecast %1%"), server;
	if (!BASS_Encode_SetNotify(encoder, EncoderNotify, 0)) // notify of dead encoder/connection
		Fatal("couldn't set callback cunction (%1%)"), BASS_ErrorGetCode();
}

BassCast::~BassCast() {} // this HAS to be here, or scoped_ptr will poop in it's pants, header won't work.
