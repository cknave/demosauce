/*
*   demosauce - fancy icecast source client
*
*   this source is published under the gpl license. google it yourself.
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*
*   classes:
*   Resample
*   ConvertFromInterleaved
*   ConvertToInterleaved
*/

#ifndef _H_CONVERT_
#define _H_CONVERT_

#include <cassert>
#include <limits>

#include <boost/utility.hpp>
#include <boost/scoped_ptr.hpp>

#include "audiostream.h"

//-----------------------------------------------------------------------------

class Resample : public Machine
{
public:
    Resample();
    virtual ~Resample();

    void process(AudioStream& stream, uint32_t const frames);
    std::string name() const;
    void set_rates(uint32_t source_rate, uint32_t out_rate);

private:
    struct Pimpl;
    boost::scoped_ptr<Pimpl> pimpl;
};

//-----------------------------------------------------------------------------

template <typename SampleType>
class ConvertFromInterleaved : boost::noncopyable
{
public:
    ConvertFromInterleaved() : _channels(0) {}

    SampleType* input_buffer(uint32_t frames, uint32_t channels)
    {
        _channels = channels;
        if (buff.size() < frames * channels)
            buff.resize(frames * channels);
        return buff.get();
    }

    void process(AudioStream& stream, uint32_t const frames);

private:
    uint32_t _channels;
    AlignedBuffer<SampleType> buff;
};

template<> inline void ConvertFromInterleaved<int16_t>
::process(AudioStream& stream, uint32_t const frames)
{
    assert(buff.size() >= frames * _channels);

    stream.set_channels(_channels);
    if (stream.max_frames() < frames)
        stream.resize(frames);
    stream.set_frames(frames);

    static float const range = 1.0 / -std::numeric_limits<int16_t>::min(); //2's complement
    for (uint_fast32_t chan = 0; chan < _channels; ++chan)
    {
        int16_t const* in = buff.get() + chan;
        float* out = stream.buffer(chan);
        for (uint_fast32_t i = frames; i; --i, in += _channels)
            *out++ = range * *in;
    }
}

template<> inline void ConvertFromInterleaved<float>
::process(AudioStream& stream, uint32_t const frames)
{
    assert(buff.size() >= frames * _channels);

    stream.set_channels(_channels);
    if (stream.max_frames() < frames)
        stream.resize(frames);
    stream.set_frames(frames);

    for (uint_fast32_t chan = 0; chan < _channels; ++chan)
    {
        float const * in = buff.get() + chan;
        float * out = stream.buffer(chan);
        for (uint_fast32_t i = frames; i; --i, in += _channels)
            *out++ = *in;
    }
}

//-----------------------------------------------------------------------------
void float_to_int16(float const * in, int16_t * out, uint32_t len);

class ConvertToInterleaved
{
public:

    template <typename MachineType>
    void set_source(MachineType & machine)
    {
        source = boost::static_pointer_cast<Machine>(machine);
    }

    template <typename SampleType>
    uint32_t process(SampleType* outSamples, uint32_t frames, uint32_t channels);

    template <typename SampleType>
    uint32_t process1(SampleType* outSamples, uint32_t frames);

    template <typename SampleType>
    uint32_t process2(SampleType* outSamples, uint32_t frames);

private:
    MachinePtr source;
    AlignedBuffer<int16_t> convert_buffer;
    AudioStream in_stream;
};

template<typename SampleType> inline uint32_t ConvertToInterleaved
::process(SampleType* out_samples, uint32_t frames, uint32_t channels)
{
    assert(channels > 0 && channels <= 2);
    if (channels == 2)
        return process2(out_samples, frames);
    return process1(out_samples, frames);
}

template<> inline uint32_t ConvertToInterleaved
::process1(int16_t* out_samples, uint32_t frames)
{
    uint32_t proc_frames = 0;

    while (proc_frames < frames)
    {
        source->process(in_stream, frames - proc_frames);
        assert(in_stream.channels() == 1);
        float_to_int16(in_stream.buffer(0), out_samples, in_stream.frames());
        out_samples += in_stream.frames();
        proc_frames += in_stream.frames();
        assert(proc_frames <= frames);
        if (in_stream.end_of_stream)
            break;
    }
    return proc_frames;
}

template<> inline uint32_t ConvertToInterleaved
::process1(float* out_samples, uint32_t frames)
{
    uint32_t proc_frames = 0;

    while (proc_frames < frames)
    {
        source->process(in_stream, frames - proc_frames);
        assert(in_stream.channels() == 1);
        memmove(out_samples, in_stream.buffer(0), in_stream.frames() * sizeof(float));
        out_samples += in_stream.frames();
        proc_frames += in_stream.frames();
        assert(proc_frames <= frames);
        if (in_stream.end_of_stream)
            break;
    }
    return proc_frames;
}

template<> inline uint32_t ConvertToInterleaved
::process2(int16_t* out_samples, uint32_t frames)
{
    uint32_t proc_frames = 0;
    int16_t* out = out_samples;

    while (proc_frames < frames)
    {
        source->process(in_stream, frames - proc_frames);
        assert(in_stream.channels() == 2);
        uint32_t const in_frames = in_stream.frames();

        if (convert_buffer.size() < in_frames * 2)
            convert_buffer.resize(in_frames * 2);
        int16_t* buff0 = convert_buffer.get();
        int16_t* buff1 = buff0 + in_frames;

        float_to_int16(in_stream.buffer(0), buff0, in_frames);
        float_to_int16(in_stream.buffer(1), buff1, in_frames);

        for (uint_fast32_t i = in_frames; i; --i)
        {
            *out++ = *buff0++;
            *out++ = *buff1++;
        }

        proc_frames += in_frames;
        assert(proc_frames <= frames);
        if (in_stream.end_of_stream)
            break;
    }
    return proc_frames;
}

template<> inline uint32_t ConvertToInterleaved
::process2(float* out_samples, uint32_t frames)
{
    uint32_t proc_frames = 0;
    float* out = out_samples;

    while (proc_frames < frames)
    {
        source->process(in_stream, frames - proc_frames);
        assert(in_stream.channels() == 2);
        uint32_t const in_frames = in_stream.frames();

        float * buff0 = in_stream.buffer(0);
        float * buff1 = in_stream.buffer(1);

        for (uint_fast32_t i = in_frames; i; --i)
        {
            *out++ = *buff0++;
            *out++ = *buff1++;
        }

        proc_frames += in_frames;
        assert(proc_frames <= frames);
        if (in_stream.end_of_stream)
            break;
    }
    return proc_frames;
}
#endif
