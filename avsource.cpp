#include <boost/version.hpp>
#if (BOOST_VERSION / 100) < 1036
#error "need at leats BOOST version 1.36"
#endif

// fixes build problems with g++
#define __STDC_CONSTANT_MACROS

#include <algorithm>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include "logror.h"
#include "convert.h"
#include "avsource.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <iostream>

using namespace std;
using boost::numeric_cast;
using namespace logror;

typedef int16_t sample_t;

struct AvSource::Pimpl
{
	void Free();
	int DecodeFrame(AVPacket& packet, uint8_t* const buffer, int const size);
	void Process(AudioStream & stream, uint32_t const frames);

	ConvertFromInterleaved<sample_t> converter;
	string fileName;

	AVFormatContext* formatContext;
	AVCodecContext* codecContext;
	AVCodec* codec;

	int audioStreamIndex;
	int ffBufferPos;
	AlignedBuffer<uint8_t> ffBuffer;

	Pimpl() :
		formatContext(0),
		codecContext(0),
		codec(0),
		audioStreamIndex(-1),
		ffBufferPos(0)
	{}
};

AvSource::AvSource():
	pimpl(new Pimpl)
{
	av_register_all();
	pimpl->Free();
}

bool AvSource::Load(string fileName)
{
	pimpl->Free();
	pimpl->fileName = fileName;

	Log(info, "avsource loading %1%"), fileName;

	if (av_open_input_file(&pimpl->formatContext, fileName.c_str(), NULL, 0, NULL) != 0)
	{
		Log(warning, "can't open file %1%"), fileName;
		return false;
	}

	if (av_find_stream_info(pimpl->formatContext) < 0)
	{
		Log(warning, "no stream information %1%"), fileName;
		return false;
	}

	for (unsigned int i = 0; i < pimpl->formatContext->nb_streams; i++) // find first stream
		if (pimpl->formatContext->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO)
		{
			pimpl->audioStreamIndex = i;
			break;
		}

	if (pimpl->audioStreamIndex == -1)
	{
		Log(warning, "no audio stream :( %1%"), fileName;
		return false;
	}

	pimpl->codecContext = pimpl->formatContext->streams[pimpl->audioStreamIndex]->codec;
	pimpl->codec = avcodec_find_decoder(pimpl->codecContext->codec_id);
	if (!pimpl->codec)
	{
		Log(warning, "unsupported codec %1%"), fileName;
		return false;
	}

	if (avcodec_open(pimpl->codecContext, pimpl->codec) < 0)
	{
		Log(warning, "failed to open codec %1%"), fileName;
		return false;
	}
	return true;
}

int AvSource::Pimpl::DecodeFrame(AVPacket& packet, uint8_t* const buffer, int const size)
{
   	int len = 0;
	int decoded_size = 0;
	//store size & data for later freeing
	uint8_t* const packet_data = packet.data;
	int const packet_size = packet.size;

	while (packet.size > 0)
	{
		int data_size = size - decoded_size;
		sample_t* const buf = reinterpret_cast<sample_t*>(buffer + decoded_size);
		len = avcodec_decode_audio3(codecContext, buf, &data_size, &packet);
		if (len < 0) // error, skip frame
			return 0;
		packet.data += len;
		packet.size -= len;
		decoded_size += data_size;
	}

	packet.data = packet_data;
	packet.size = packet_size;
	return decoded_size;
}

void AvSource::Pimpl::Process(AudioStream& stream, uint32_t const frames)
{
	uint32_t const channels = numeric_cast<uint32_t>(codecContext->channels);
	if (ffBufferPos < 0 || channels < 1)
	{
		Error("strange state");
		stream.Zero(frames);
		stream.endOfStream = true;
		return;
	}

	int const needBytes = FramesInBytes<sample_t>(frames, channels);
	// somehow small buffers seem to cause trouble so I read moar
	int const minBytes = max(192000, needBytes);
	int const bufferSize = max(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3, minBytes * 2);
	if (ffBuffer.Size() < static_cast<size_t>(bufferSize))
	    ffBuffer.Resize(bufferSize);

	AVPacket packet;
   	while (ffBufferPos < minBytes)
	{
        if (av_read_frame(formatContext, &packet) < 0) // demux/read packet
			break; // end of stream
		if (packet.stream_index == audioStreamIndex)
			ffBufferPos += DecodeFrame(packet, ffBuffer.Get() + ffBufferPos, bufferSize);
		else
			av_free_packet(&packet);
	}

	// according to the docs, av_free_packet should be called at some point
	// without these checks, a segfault might occour
 	if (packet.data && packet.size && packet.stream_index == audioStreamIndex)
		av_free_packet(&packet);

    stream.endOfStream = ffBufferPos < needBytes;
    size_t const usedBytes = min(needBytes, ffBufferPos);
    uint32_t const usedFrames = BytesInFrames<uint32_t, sample_t>(usedBytes, channels);
	sample_t* const convBuffer = converter.Buffer(frames, channels);

	memcpy(convBuffer, ffBuffer.Get(), usedBytes);
    converter.Process(stream, usedFrames);
    memmove(ffBuffer.Get(), ffBuffer.Get() + usedBytes, ffBufferPos - usedBytes);
    ffBufferPos -= usedBytes;
    if(stream.endOfStream) LogDebug("eos avcodec %1% frames left"), stream.Frames();
}

void AvSource::Process(AudioStream & stream, uint32_t const frames)
{
	pimpl->Process(stream, frames);
}

void AvSource::Pimpl::Free()
{
	if (codecContext)
		avcodec_close(codecContext);
	if (formatContext)
		av_close_input_file(formatContext);
	codec = 0;
	codecContext = 0;
	formatContext = 0;
	ffBufferPos = 0;
}

AvSource::~AvSource()
{
	pimpl->Free();
}

uint32_t AvSource::Channels() const
{
	return numeric_cast<uint32_t>(pimpl->codecContext->channels);
}

uint32_t AvSource::AvSource::Samplerate() const
{
	return numeric_cast<uint32_t>(pimpl->codecContext->sample_rate);
}

uint32_t AvSource::Bitrate() const
{
	return numeric_cast<uint32_t>(pimpl->codecContext->bit_rate) / 1000;
}

double AvSource::Duration() const
{
	return numeric_cast<double>(pimpl->formatContext->duration) / AV_TIME_BASE;
}

bool AvSource::CheckExtension(string fileName)
{
	boost::filesystem::path file(fileName);
	string name = file.filename();
	static size_t const elements = 17;
	char const * ext[elements] = {".mp3", ".ogg", ".m4a", ".wma", ".acc", ".flac", ".mp4", ".ac3",
		".wav", ".ape", ".wv", ".mpc", ".mp+", ".mpp", ".ra", ".mp2", ".mp1"};
	for (size_t i = 0; i < elements; ++i)
		if (boost::iends_with(name, ext[i]))
			return true;
	return false;
}

// I guess this got introduced with some ffpmep update
#undef CodecType

string AvSource::CodecType() const
{
	CodecID codecType = pimpl->codec->id;
	if (codecType >= CODEC_ID_PCM_S16LE && codecType <= CODEC_ID_PCM_BLURAY)
		return "pcm";
	if (codecType >= CODEC_ID_ADPCM_IMA_QT && codecType <= CODEC_ID_ADPCM_IMA_ISS)
		return "adpcm";
	switch (codecType)
	{
		case CODEC_ID_RA_144:
		case CODEC_ID_RA_288: return "real";
		case CODEC_ID_MP2: return "mp2";
		case CODEC_ID_MP3: return "mp3";
		case CODEC_ID_AAC: return "aac";
		case CODEC_ID_AC3: return "ac3";
		case CODEC_ID_VORBIS: return "vorbis";
		case CODEC_ID_WMAV1:
		case CODEC_ID_WMAV2:
		case CODEC_ID_WMAVOICE:
		case CODEC_ID_WMAPRO:
		case CODEC_ID_WMALOSSLESS: return "wma";
		case CODEC_ID_FLAC: return "flac";
		case CODEC_ID_WAVPACK: return "wavpack";
		case CODEC_ID_APE: return "monkey";
		case CODEC_ID_MUSEPACK7:
		case CODEC_ID_MUSEPACK8: return "musepack";
		case CODEC_ID_MP1: return "mp1";
		case CODEC_ID_MP4ALS: return "mp4";
		default: return "unimportant";
	}
}
