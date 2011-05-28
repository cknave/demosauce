/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

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
    uint32_t ChunkID;       // "RIFF"
    uint32_t ChunkSize;     // writtenBytes - 8
    uint32_t Format;        // "WAVE"
    uint32_t Subchunk1ID;   // "fmt "
    uint32_t SubChunk1Size; // 16 for pcm
    uint16_t AudioFormat;   // 1 for pcm, 3 float
    uint16_t NumChannels;   // 2
    uint32_t SampleRate;    // 44100
    uint32_t ByteRate;      // samplerate * numchannels * sizeof(sample)
    uint16_t BlockAlign;    // numchannels * sizeof(sample)
    uint16_t BitsPerSample; // 16
    uint32_t Subchunk2ID;   // "data"
    uint32_t SubChunk2Size; // numsamples * sumchannels * sizeof(sample)
};

class WavWriter
{
public:
    WavWriter(std::string file_name, uint32_t samplerate, uint16_t channels, bool is_float_data = false) :
        samplerate(samplerate),
        channels(channels),
        is_float_data(is_float_data)
    {
        file.open(file_name.c_str(), std::ios::binary);
        if (file)
        {
            WavHeader dummy;
            ::write(&dummy, sizeof(WavHeader));
        }
        else
        {
            std::cout << "failed to open file for writing: " << file_name << std::endl;
        }
    }
    virtual ~WavWriter()
    {
        if (!file) return;

        unsigned sample_size = is_float_data ? sizeof(float) : sizeof(int16_t);
        unsigned file_size = static_cast<unsigned>(file.tellp());

        WavHeader header;
        header.ChunkID          = to_big(0x52494646);
        header.ChunkSize        = to_little(file_size - 8);
        header.Format           = to_big(0x57415645);
        header.Subchunk1ID      = to_big(0x666d7420);
        header.SubChunk1Size    = to_little(16);
        header.AudioFormat      = to_little(is_float_data ? 3 : 1);
        header.NumChannels      = to_little(channels);
        header.SampleRate       = to_little(samplerate);
        header.ByteRate         = to_little(samplerate * channels * sample_size);
        header.BlockAlign       = to_little(channels * sample_size);
        header.BitsPerSample    = to_little(8 * sample_size);
        header.Subchunk2ID      = to_big(0x64617461);
        header.SubChunk2Size    = to_little(file_size - sizeof(WavHeader));
        file.seekp(0);
        file.write(reinterpret_cast<char*>(&header), sizeof(WavHeader));
        file.close();
    }

    template <typename T>
    void write(T* data, size_t size)
    {
        if (file)
        {
            file.write(reinterpret_cast<char*>(data), size);
        }
    }

private:
    uint32_t samplerate;
    uint16_t channels;
    bool is_float_data;
    std::ofstream file;
};

#endif
