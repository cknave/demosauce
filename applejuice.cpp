#include <cstdlib>
#include <string>
#include <iostream>
#include <vector>

#include <boost/format.hpp>
#include <boost/shared_ptr.hpp>

#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_thread.h>

#include "libreplaygain/replay_gain.h"

#include "logror.h"
#include "dsp.h"
#include "convert.h"
#include "basssource.h"
#include "avsource.h"

// linked resources

extern void* _binary_res_background_png_size;
extern void* _binary_res_background_png_start;
static int const bg_size = reinterpret_cast<int>(&_binary_res_background_png_size);
static char* const bg_ptr = reinterpret_cast<char*>(&_binary_res_background_png_start);

extern void* _binary_res_buttons_png_size;
extern void* _binary_res_buttons_png_start;
static int const buttons_size = reinterpret_cast<int>(&_binary_res_buttons_png_size);
static char* const buttons_ptr = reinterpret_cast<char*>(&_binary_res_buttons_png_start);

extern void* _binary_res_icon_png_size;
extern void* _binary_res_icon_png_start;
static int const icon_size = reinterpret_cast<int>(&_binary_res_icon_png_size);
static char* const icon_ptr = reinterpret_cast<char*>(&_binary_res_icon_png_start);

extern void* _binary_res_font_synd_png_size;
extern void* _binary_res_font_synd_png_start;
static int const font_size = reinterpret_cast<int>(&_binary_res_font_synd_png_size);
static char* const font_ptr = reinterpret_cast<char*>(&_binary_res_font_synd_png_start);

// structs

struct Button
{
	void (*OnClick) ();
	bool pressed;
	SDL_Rect pos;
	SDL_Rect up;
	SDL_Rect down;
	SDL_Surface* surface;
};

struct BitmapFont
{
	static int const max_chars = 95;
	int offset[max_chars];
	int width[max_chars];
	int height;
	SDL_Surface* surface;
};

class Mutex
{
	SDL_sem* sem;

public:
	Mutex() : sem(SDL_CreateSemaphore(1)) {}
	~Mutex() { SDL_DestroySemaphore(sem); }

	void Lock()
	{
		SDL_SemWait(sem);
	}

	void Unlock()
	{
		if (SDL_SemValue(sem) == 0)
			SDL_SemPost(sem);
	}
};

template<typename T> class AtomicValue // T must be copyable
{
	T val;
	Mutex mutex;

public:
	AtomicValue(T value) : val(value) {}

	T Get()
	{
		mutex.Lock();
		T value = val;
		mutex.Unlock();
		return value;
	}

	void Set(T value)
	{
		mutex.Lock();
		val = value;
		mutex.Unlock();
	}
};

// global constants
#define SAMPLERATE 44100
#define CHANNELS 2
#define BUFFER_SIZE (44100 * 2)

// global variables

SDL_Thread* scan_thread = NULL;
SDL_Thread* play_thread = NULL;
SDL_Surface* screen = NULL;
SDL_Surface* surface_bg = NULL;
SDL_AudioSpec audio_spec;
SDL_Rect bg_rect = {0, 0, 320, 200};
SDL_Rect txt_rect = {128, 48, 184, 112};

Uint32 bg_color = 0;
BitmapFont font;
std::string file_name;
std::vector<Button> buttons;
ConvertToInterleaved converter;
boost::shared_ptr<Gain> gain = boost::shared_ptr<Gain>(new Gain());
boost::shared_ptr<Peaky> peaky = boost::shared_ptr<Peaky>(new Peaky());
AtomicValue<double> amp_replaygain(1);

// sdl has rather small buffers -> mutil-threaded dual buffering to reduce overhead \o/
AlignedBuffer<uint8_t> convert_buffer(BUFFER_SIZE);
AlignedBuffer<uint8_t> play_buffer(BUFFER_SIZE * 2);
uint8_t* play_buffer_a = play_buffer.Get();
uint8_t* play_buffer_b = play_buffer.Get() + BUFFER_SIZE;
size_t buffer_offset = 0;

Mutex read_mutex;
Mutex write_mutex;
AtomicValue<bool> playbeack_stopped(true);
bool stream_ended = true;
int remaining_bytes = 0;

// font functions

void ParseBitmapFont(BitmapFont& font, SDL_Surface* surface)
{
	Uint8* pixels = reinterpret_cast<Uint8*>(surface->pixels);
	font.height = surface->h;
	font.surface = surface;
	int ch, last_offset = 0;
	for (int i = 0; i < surface->w && ch < BitmapFont::max_chars; ++i)
		if (*(pixels + i) && i)
		{
			font.offset[ch] = last_offset;
			font.width[ch++] = i - last_offset;
			last_offset = i;
		}
}

int PaintTextLine(std::string const & text, int x, int y, BitmapFont& font, Uint32 value)
{
	int total_with = 0;
	Uint8* font_buffer = reinterpret_cast<Uint8*>(font.surface->pixels);
	Uint8* screen_buffer = reinterpret_cast<Uint8*>(screen->pixels);
	for (size_t i = 0; i < text.size(); ++i)
	{
		int ch = text[i] - ' ';
		ch = (ch < 0 || ch >= BitmapFont::max_chars) ? 0 : ch;
		if (total_with + font.width[ch] >= txt_rect.w)
			continue;
		for (int yy = 0; yy < font.height - 1; ++yy)
		{
			Uint8* font_pix = font_buffer + (yy + 1) * font.surface->pitch + font.offset[ch];
			Uint32* screen_pix = reinterpret_cast<Uint32*>(screen_buffer
				+ (y + yy) * screen->pitch + (x + total_with) * sizeof(Uint32));
			for (int xx = 0; xx < font.width[ch]; ++xx, ++font_pix, ++screen_pix)
				if (*font_pix)
					*screen_pix = value;
		}
		total_with += font.width[ch];
	}
	SDL_UpdateRect(screen, x, y, total_with, font.height);
	return total_with;
}

void PaintText(std::string const & text, int x, int y, BitmapFont& font, Uint32 value)
{
	size_t begin = 0;
	int line = 0;
	for (size_t i = 0; i <= text.size(); ++i)
		if (i == text.size() || text[i] == '\n')
		{
			PaintTextLine(text.substr(begin, i - begin), x, y + line++ * font.height, font, value);
			begin = i + 1;
		}
}

void RedrawTextArea(std::string msg)
{
	SDL_LockSurface(screen);
	SDL_FillRect(screen, &txt_rect, bg_color);
	PaintText(msg, txt_rect.x + 5, txt_rect.y + 5, font, 0);
	SDL_UnlockSurface(screen);
	SDL_Rect blit_rect = {txt_rect.x + bg_rect.x, txt_rect.y, txt_rect.w, txt_rect.h};
	SDL_BlitSurface(surface_bg, &blit_rect, screen, &txt_rect);
	SDL_UpdateRect(screen, txt_rect.x, txt_rect.y, txt_rect.w, txt_rect.h);
}

// ui functions

void HandleMouseClick(int x, int y)
{
	for (size_t i = 0; i < buttons.size(); ++i)
	{
		Button & button = buttons[i];
		SDL_Rect& r = button.pos;
		bool over = (x > r.x) && (y > r.y) && (x < r.x + r.w) && (y < r.y + r.h);
		if (over && button.OnClick)
			button.OnClick();
	}
}

void PaintButton(Button& button)
{
	SDL_Rect* rect = button.pressed ? &button.down : &button.up;
	SDL_BlitSurface(button.surface, rect, screen, &button.pos);
	SDL_UpdateRect(screen, button.pos.x, button.pos.y, button.pos.w, button.pos.h);
}

void UpdateButtons(bool lmbPressed, int x, int y)
{
	for (size_t i = 0; i < buttons.size(); ++i)
	{
		Button & button = buttons[i];
		SDL_Rect& r = button.pos;
		bool over = (x > r.x) && (y > r.y) && (x < r.x + r.w) && (y < r.y + r.h);
		if ((over && button.pressed != lmbPressed) || (!over && button.pressed))
		{
			button.pressed = over && lmbPressed;
			PaintButton(button);
		}
	}
}

// audio functions

std::string ScanSong(std::string fileName)
{
	BassSource bassSource;
	AvSource avSource;
	bool bassLoaded = bassSource.Load(fileName, false);
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
		return "problem:\nunknown format";
	if (samplerate == 0)
		return "problem:\nsamplerate is zero";
	if (channels < 1 || channels > 2)
		return "problem:\nunsupported number\nof channels";

	uint64_t frameCounter = 0;
	RG_SampleFormat format = {samplerate, RG_FLOAT_32_BIT, channels, FALSE};
	RG_Context * context = RG_NewContext(&format);

	AudioStream stream;
	while (!stream.endOfStream)
	{
		decoder->Process(stream, 48000);
		float* buffers[2] = {stream.Buffer(0), channels == 2 ? stream.Buffer(1) : NULL};
		RG_Analyze(context, buffers, stream.Frames());
		frameCounter += stream.Frames();
	}
	double replayGain = RG_GetTitleGain(context);
	RG_FreeContext(context);

	length = static_cast<double>(frameCounter) / samplerate;
	amp_replaygain.Set(DbToAmp(replayGain));

	std::string msg = "library: %1%\ntype/codec: %2%\nlength: %3%\nreplay gain: %4%\n";
	if (bassSource.IsModule())
		msg.append("loopiness: %7%");
	else
		msg.append("bitrate: %5%\nsamplerate: %6%");

	std::string decoder_name = bassLoaded ? "bass" : "avcodec";
	boost::format formater(msg);
	formater.exceptions(boost::io::no_error_bits);
	return str(formater % decoder_name % type
		% length % replayGain % bitrate % samplerate % bassSource.Loopiness());
}

template <typename T> boost::shared_ptr<T> new_shared()
{
	return boost::shared_ptr<T>(new T);
}

bool LoadSong(std::string fileName)
{
	if (fileName.size() == 0)
		return false;

	boost::shared_ptr<MachineStack> machineStack = new_shared<MachineStack>();
	converter.SetSource(machineStack);
	boost::shared_ptr<BassSource> bassSource = new_shared<BassSource>();
	boost::shared_ptr<AvSource> avSource = new_shared<AvSource>();

	bool bassLoaded = bassSource->Load(fileName, false);
	bool avLoaded = bassLoaded ? false : avSource->Load(fileName);
	if (!avLoaded && !bassLoaded)
		return false;

	uint32_t channels = 0;
	uint32_t samplerate = 0;

	if (bassLoaded)
	{
		channels = bassSource->Channels();
		samplerate = bassSource->Samplerate();
		machineStack->AddMachine(bassSource);
	}

	if (avLoaded)
	{
		channels = avSource->Channels();
		samplerate = avSource->Samplerate();
		machineStack->AddMachine(avSource);
	}

	if (samplerate != SAMPLERATE)
	{
		boost::shared_ptr<Resample> resample = new_shared<Resample>();
		resample->Set(samplerate, SAMPLERATE);
		machineStack->AddMachine(resample);
	}

	if (bassSource->IsAmigaModule())
	{
		boost::shared_ptr<MixChannels> mixChannels = new_shared<MixChannels>();
		mixChannels->Set(.7, .3, .7, .3);
		machineStack->AddMachine(mixChannels);
	}

	if (bassSource->IsModule() && bassSource->Loopiness() > 0.1)
	{
		boost::shared_ptr<LinearFade> fade = new_shared<LinearFade>();
		double duration = std::max(bassSource->Duration(), 120.0);
		bassSource->SetLoopDuration(duration);
		fade->Set(SAMPLERATE * (duration - 5), SAMPLERATE * (duration - 1), 1, 0);
		machineStack->AddMachine(fade);
	}

	if (channels != CHANNELS)
	{
		boost::shared_ptr<MapChannels> mapChannels = new_shared<MapChannels>();
		mapChannels->SetOutChannels(CHANNELS);
		machineStack->AddMachine(mapChannels);
	}

	//machineStack->AddMachine(gain);
	machineStack->AddMachine(peaky);
	machineStack->UpdateRouting();
	return true;
}

//uncomment to dump output
#include "wav.h"
WavWriter wav("dump.wav", 44100, 2, sizeof(int16_t));

bool StreamWriter()
{
	uint32_t frames = BytesInFrames<uint32_t, int16_t>(BUFFER_SIZE, CHANNELS);
	int16_t* buffer = reinterpret_cast<int16_t*>(convert_buffer.Get());
	gain->SetAmp(amp_replaygain.Get()); // in case playback was started before scan finished

	uint32_t procFrames = converter.Process2(buffer, frames);
	size_t read_bytes = FramesInBytes<int16_t>(procFrames, CHANNELS);
	wav.write(buffer, read_bytes);

	write_mutex.Lock(); // unlocked by reader when more data is needed
	read_mutex.Lock();
	memcpy(play_buffer_b, buffer, read_bytes);
	stream_ended = procFrames != frames;
	read_mutex.Unlock();

	return procFrames == frames;
}

void AudioCallback(void* userdata, Uint8* buffer, int len)
{
	// TODO: support large output buffers. apparently pulse provides ratherbig ones
	assert(len <= BUFFER_SIZE);

	if (playbeack_stopped.Get() || remaining_bytes < 0)
		SDL_PauseAudio(1);
	remaining_bytes -= len;

	if (buffer_offset + len <= BUFFER_SIZE)
	{
		memcpy(buffer, play_buffer_a + buffer_offset, len);
		buffer_offset += len;
	}
	else
	{
		size_t rest = BUFFER_SIZE - buffer_offset;
		memcpy(buffer, play_buffer_a + buffer_offset, rest);
		memset(play_buffer_a, 0, BUFFER_SIZE);  // in case writer isnt't ready

		read_mutex.Lock();

		remaining_bytes += stream_ended ? 0 : BUFFER_SIZE;
		memcpy(buffer + rest, play_buffer_b, len - rest);
		buffer_offset = len - rest;
		std::swap(play_buffer_a, play_buffer_b);

		read_mutex.Unlock();
		write_mutex.Unlock(); // tell writer we need moar data
	}
}

int RunScanFile(void* data)
{
	if (data) // clean up old thread... required for linux it seems
		SDL_WaitThread(reinterpret_cast<SDL_Thread*>(data), NULL);
	RedrawTextArea("scanning file...\n\nthis might take a few\nseconds.");
	std::string msg = ScanSong(file_name);
	RedrawTextArea(msg);
	return 0;
}

int RunStreamWriter(void* data)
{
	if (data) // clean up old thread...
		SDL_WaitThread(reinterpret_cast<SDL_Thread*>(data), NULL);
	while(StreamWriter());
	LogDebug("peak %1%"), peaky->Peak();
	return 0;
}

// user events

void StartPlayback()
{
	SDL_AudioSpec wanted;
	wanted.freq = SAMPLERATE;
	wanted.format = AUDIO_S16SYS;
	wanted.channels = CHANNELS;
	wanted.samples = SAMPLERATE;
	wanted.callback = AudioCallback;

	bool audio_ok = SDL_GetAudioStatus() != SDL_AUDIO_STOPPED;
	if (audio_ok)
		SDL_PauseAudio(1);
	else
		SDL_OpenAudio(&wanted, &audio_spec);

	audio_ok = (wanted.freq == audio_spec.freq &&
		wanted.format == audio_spec.format &&
		wanted.channels == audio_spec.channels);
	if (!audio_ok || !LoadSong(file_name))
	{
		SDL_CloseAudio();
		return;
	}

	playbeack_stopped.Set(false);
	stream_ended = false;
	remaining_bytes = BUFFER_SIZE * 3;
	gain->SetAmp(amp_replaygain.Get());
	play_buffer.Zero();
	convert_buffer.Zero();
	play_thread = SDL_CreateThread(RunStreamWriter, play_thread);

	SDL_PauseAudio(0);
}

void StopPlayback()
{
	playbeack_stopped.Set(true);
}

void Exit()
{
	exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[])
{
	logror::LogSetConsoleLevel(logror::debug);
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
	atexit(SDL_Quit);

	// load icon & create window
	SDL_RWops* rw_icon = SDL_RWFromMem(icon_ptr, icon_size);
	SDL_Surface* surface_icon = IMG_LoadTyped_RW(rw_icon, 1, const_cast<char*>("PNG"));
	SDL_putenv(const_cast<char*>("SDL_VIDEO_CENTERED=center"));
	SDL_WM_SetCaption("Nectarine Apple Juice", NULL);
	SDL_WM_SetIcon(surface_icon, NULL);
	screen = SDL_SetVideoMode(bg_rect.w, bg_rect.h, 32, SDL_SWSURFACE);

	// load & draw background
	srand(reinterpret_cast<unsigned>(screen));
	bg_color = SDL_MapRGB(screen->format, 0xb0, 0xb0, 0xc4);
	SDL_RWops* rw_bg = SDL_RWFromMem(bg_ptr, bg_size);
	surface_bg = IMG_LoadTyped_RW(rw_bg, 1, const_cast<char*>("PNG"));
	bg_rect.x = bg_rect.w * (rand() % 4);
	SDL_BlitSurface(surface_bg, &bg_rect, screen, NULL);
	SDL_UpdateRect(screen, 0, 0, 0, 0);

	// load & draw buttons
	SDL_RWops* rw_buttons = SDL_RWFromMem(buttons_ptr, buttons_size);
	SDL_Surface* surface_buttons = IMG_LoadTyped_RW(rw_buttons, 1, const_cast<char*>("PNG"));
	for (size_t i = 0; i < 3; ++i)
	{
		SDL_Rect button_pos = {170 + 50 * i, 168, 40, 24};
		SDL_Rect button_up = {80 * i, 0, 40, 24};
		SDL_Rect button_down = {40 + 80 * i, 0, 40, 24};
		Button button = {NULL, false, button_pos, button_up, button_down, surface_buttons};
		buttons.push_back(button);
		PaintButton(button);
	}
	buttons[0].OnClick = StartPlayback;
	buttons[1].OnClick = StopPlayback;
	buttons[2].OnClick = Exit;

	// load font, draw welcome msg
	SDL_RWops* rw_font = SDL_RWFromMem(font_ptr, font_size);
	SDL_Surface* surface_font = IMG_LoadTyped_RW(rw_font, 1, const_cast<char*>("PNG"));
	ParseBitmapFont(font, surface_font);
	char const * msg = "Hi!\nCurrently the only way\nto load a song is by\ncommand line.\n\n  enjoy, ~maep";
	RedrawTextArea(msg);

	// scan file from command line, if present
	if (argc > 1)
	{
		file_name = argv[1];
		scan_thread = SDL_CreateThread(RunScanFile, scan_thread);
	}

	// event loop
	SDL_Event event;
	bool quit = false;
	while(!quit && SDL_WaitEvent(&event))
		switch(event.type)
		{
		case SDL_KEYUP:
			quit = event.key.keysym.sym == SDLK_ESCAPE;
			if (event.key.keysym.sym == SDLK_SPACE)
			{
				if (playbeack_stopped.Get())
					StartPlayback();
				else
					StopPlayback();
			}
			break;
		case SDL_MOUSEMOTION:
			UpdateButtons(event.motion.state & SDL_BUTTON(1), event.motion.x, event.motion.y);
			break;
		case SDL_MOUSEBUTTONDOWN:
			UpdateButtons(true, event.button.x, event.button.y);
			break;
		case SDL_MOUSEBUTTONUP:
			HandleMouseClick(event.button.x, event.button.y);
			UpdateButtons(false, event.button.x, event.button.y);
			break;
		case SDL_QUIT:
			quit = true;
			break;
		default:
			break;
		}
	return EXIT_SUCCESS;
}
