#ifndef _H_WAV_
#define _H_WAV_

#include <string>
#include <iostream>
#include <fstream>
#include <boost/detail/endian.hpp>

#if defined(_WIN32)
	#include <intrin.h>
	#define swap32(value) _byteswap_ulong(value)
#elif defined(__unix__)
	#define swap32(value) __builtin_bswap32(value)
#else
	#error missing byte swap function
#endif

#if defined(BOOST_BIG_ENDIAN)
	#define to_big(value) (value)
	#define to_little swap32(value)
#else
	#define to_big(value) swap32(value)
	#define to_little(value) (value)
#endif

struct WavHeader
{
	uint32_t ChunkID; // "RIFF"
	uint32_t ChunkSize; // writtenBytes - 8
	uint32_t Format; // "WAVE"
	uint32_t Subchunk1ID; // "fmt "
	uint32_t SubChunk1Size; // 16 for pcm
	uint16_t AudioFormat; // 1 for pcm
	uint16_t NumChannels; // 2
	uint32_t SampleRate; // 44100
	uint32_t ByteRate; //  samplerate * numchannels * sizeof(sample)
	uint16_t BlockAlign; // numchannels * sizeof(sample)
	uint16_t BitsPerSample; // 16
	uint32_t Subchunk2ID; // "data"
	uint32_t SubChunk2Size; // numsamples * sumchannels * sizeof(sample) 
};

class WavWriter
{
public:
	WavWriter(std::string fileName, uint32_t samplerate, uint16_t channels, uint16_t sample_size) :
		samplerate(samplerate),
		channels(channels),
		sample_size(sample_size)
	{
		file.open(fileName.c_str(), std::ios::binary);
		WavHeader header;
		memset(&header, 0, sizeof(WavHeader));
		file.write(reinterpret_cast<char*>(&header), sizeof(WavHeader));
	}
	~WavWriter()
	{
		WavHeader header;
		header.ChunkID = to_big(0x52494646);
		header.ChunkSize = to_little(static_cast<unsigned>(file.tellp()) - 8);
		header.Format = to_big(0x57415645);
		header.Subchunk1ID = to_big(0x666d7420);
		header.SubChunk1Size = to_little(16);
		header.AudioFormat = to_little(1);
		header.NumChannels = to_little(channels);
		header.SampleRate = to_little(samplerate);
		header.ByteRate = to_little(samplerate * channels * sample_size);
		header.BlockAlign = to_little(channels * sample_size);
		header.BitsPerSample = to_little(8 * sample_size);
		header.Subchunk2ID = to_big(0x64617461);
		header.SubChunk2Size = to_little(static_cast<unsigned>(file.tellp()) - sizeof(WavHeader));
		file.seekp(0);
		file.write(reinterpret_cast<char*>(&header), sizeof(WavHeader));
		file.close();
	}
	void write(void* data, size_t size)
	{
		file.write(reinterpret_cast<char*>(data), size);
	}
	
private:
	uint32_t const samplerate;
	uint16_t const channels;
	uint16_t const sample_size;
	std::ofstream file;	
};

#endif
