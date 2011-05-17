/*
*   demosauce - fancy icecast source client
*
*   this source is published under the gpl license. google it yourself.
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*
*   classes:
*   AlignedBuffer
*   AudioStream
*   Machine
*   Decoder
*   ZeroSource
*
*   functions:
*   frames_in_bytes
*   bytes_in_frames
*   unsigned_min
*/

#ifndef _AUDIO_STREAM_H_
#define _AUDIO_STREAM_H_

#include <cassert>
#include <string>

#include <boost/cstdint.hpp>
#include <boost/utility.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include "mem.h"

#ifndef NO_OVERRUN_ASSERT
    #define OVERRUN_ASSERT(arg) assert(arg)
#else
    #define OVERRUN_ASSERT(arg)
#endif

//-----------------------------------------------------------------------------

template <class T>
class AlignedBuffer : boost::noncopyable
{
public:
    AlignedBuffer() :
        _buff(0),
        _size(0)
    {}

    AlignedBuffer(size_t const size) :
        _buff(0),
        _size(size)
    {
        resize(size);
    }

    ~AlignedBuffer()
    {
        OVERRUN_ASSERT(no_overrun());
        free(_buff);
    }

    T* resize(size_t const size)
    {
        OVERRUN_ASSERT(no_overrun());

        // leave room for magic number
        _size = size;
        _buff = aligned_realloc(_buff, _size * sizeof(T) + sizeof(uint32_t));
        *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(_buff) + _size * sizeof(T)) = MAGIC_NUMBER;

        return reinterpret_cast<T*>(_buff);
    }

    T* get() const
    {
        OVERRUN_ASSERT(no_overrun());
        return reinterpret_cast<T*>(_buff);
    }

    char* get_char() const
    {
        OVERRUN_ASSERT(no_overrun());
        return reinterpret_cast<char*>(_buff);
    }

    unsigned char* get_uchar() const
    {
        OVERRUN_ASSERT(no_overrun());
        return reinterpret_cast<unsigned char*>(_buff);
    }

    size_t size() const
    {
        OVERRUN_ASSERT(no_overrun());
        return _size;
    }

    size_t size_bytes() const
    {
        OVERRUN_ASSERT(no_overrun());
        return _size * sizeof(T);
    }

    void zero()
    {
        OVERRUN_ASSERT(no_overrun());
        memset(_buff, 0, _size * sizeof(T));
    }

    void zero_end(size_t size)
    {
        OVERRUN_ASSERT(no_overrun());
        size_t start = _size - std::min(size, _size);
        memset(get() + start, 0, (_size - start) * sizeof(T));
    }

    bool no_overrun() const
    {
        if (!_buff)
        {
            return true;
        }
        return *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(_buff) + _size * sizeof(T)) == MAGIC_NUMBER;
    }

private:
    const static uint32_t MAGIC_NUMBER = 0xaa55aa55;
    void* _buff;
    size_t _size;
};

//-----------------------------------------------------------------------------

// currently only supports max two channels
class AudioStream : boost::noncopyable
{
public:
    AudioStream() :
        end_of_stream(false),
        _frames(0),
        _max_frames(0),
        _channels(2)
    {
        _buff[0] = 0;
        _buff[1] = 0;
    }

    virtual ~AudioStream()
    {
        OVERRUN_ASSERT(no_overrun());
        free(_buff[0]);
        free(_buff[1]);
    }

    void resize(uint32_t frames)
    {
        OVERRUN_ASSERT(no_overrun());
        if (_max_frames == frames)
        {
            return;
        }
        _max_frames = frames;
        size_t buffer_size = sizeof(float) * (frames + 1);
        for (uint32_t i = 0; i < _channels; ++i)
        {
            void* buff = aligned_realloc(_buff[i], buffer_size);
            _buff[i] = reinterpret_cast<float*>(buff);
            _buff[i][_max_frames] = MAGIC_NUMBER;
        }
    }

    float* buffer(uint32_t channel) const
    {
        OVERRUN_ASSERT(no_overrun());
        return _buff[channel];
    }

    uint8_t* bytified_buffer(uint32_t channel) const
    {
        return reinterpret_cast<uint8_t*>(buffer(channel));
    }

    uint32_t channels() const
    {
        return _channels;
    }

    uint32_t frames() const
    {
        OVERRUN_ASSERT(no_overrun());
        return _frames;
    }

    uint32_t max_frames() const
    {
        return _max_frames;
    }

    void set_channels(uint32_t const channels)
    {
        assert(channels == 1 || channels == 2);
        if (channels != _channels)
        {
            _channels = channels;
            resize(_max_frames);
        }
    }

    void set_frames(uint32_t frames)
    {
        OVERRUN_ASSERT(no_overrun());
        assert(frames <= _max_frames);
        _frames = frames;
    }

    size_t channel_bytes() const
    {
        OVERRUN_ASSERT(no_overrun());
        return _frames * sizeof(float);
    }

    bool no_overrun() const
    {
        for (uint32_t i = 0; i < _channels; ++i)
            if (_buff[i] && _buff[i][_max_frames] != MAGIC_NUMBER)
                return false;
        return true;
    }

    void append(AudioStream& stream)
    {
        OVERRUN_ASSERT(no_overrun());
        if (_frames + stream.frames() > _max_frames)
            resize(_frames + stream.frames());
        for (uint32_t i = 0; i < _channels && i < stream.channels(); ++i)
            memmove(_buff[i] + _frames, stream.buffer(i), stream.channel_bytes());
        _frames += stream.frames();
    }

    void append(AudioStream& stream, uint32_t frames)
    {
        OVERRUN_ASSERT(no_overrun());
        frames = std::min(frames, stream.frames());
        if (_frames + frames > _max_frames)
            resize(_frames + stream.frames());
        size_t channel_bytes = frames * sizeof(float);
        for (uint32_t i = 0; i < _channels && i < stream.channels(); ++i)
            memmove(_buff[i] + _frames, stream.buffer(i), channel_bytes);
        _frames += frames;
    }

    void drop(uint32_t frames)
    {
        OVERRUN_ASSERT(no_overrun());
        assert(frames <= _frames);

        uint32_t remaining_frames = _frames - frames;

        if (remaining_frames > 0)
            for (uint32_t i = 0; i < _channels; ++i)
                memmove(_buff[i], _buff[i] + frames, remaining_frames * sizeof(float));

        _frames = remaining_frames;
    }

    void zero(uint32_t offset, uint32_t frames)
    {
        OVERRUN_ASSERT(no_overrun());
        assert(offset + frames <= _max_frames);

        for (uint32_t i_chan = 0; i_chan < _channels; ++i_chan)
            memset(_buff[i_chan] + offset, 0, _frames * sizeof(float));
    }

    bool end_of_stream;

private:
    uint32_t _frames;
    uint32_t _max_frames;
    uint32_t _channels;
    float* _buff[2]; // meh i'd rather have arbitrary number of chans
    static float const MAGIC_NUMBER = 567.89;
};

//-----------------------------------------------------------------------------

class Machine;
typedef boost::shared_ptr<Machine> MachinePtr;

class Machine : boost::noncopyable
{
public:
    Machine() :
        source(),
        is_enabled(true)
    {}

    virtual void process(AudioStream& stream, uint32_t frames) = 0;

    virtual std::string name() const = 0;

    template<class T> void set_source(T& machine);

    void set_enabled(bool enabled)
    {
        is_enabled = enabled;
    }

     bool enabled() const
     {
         return is_enabled;
     }

protected:
    MachinePtr source;

private:
    void set_source_machine(MachinePtr& machine)
    {

    }

    bool is_enabled;
};
template<class T> inline void Machine::set_source(T& machine)
{
    MachinePtr base_machine = boost::static_pointer_cast<Machine>(machine);
    if (base_machine.get() != this)
    {
        source = machine;
    }
}

//-----------------------------------------------------------------------------

class Decoder : public Machine
{
public:
    // manipulators
    virtual bool load(std::string url) = 0;
    virtual void seek(uint64_t frame) = 0;

    // observers
    virtual uint64_t length() const = 0;
    virtual uint32_t channels() const = 0;
    virtual uint32_t samplerate() const = 0; // must never return 0
    virtual bool seekable() const = 0;

    /*  metadata keys:
    *   codec_type
    *   artist
    *   title
    *   album
    */
    virtual std::string metadata(std::string key) const = 0;
};

//-----------------------------------------------------------------------------

class ZeroSource : public Machine
{
public:
    ZeroSource() {};

    void process(AudioStream& stream, uint32_t frames)
    {
        if (stream.max_frames() < frames)
        {
            stream.resize(frames);
        }
        stream.zero(0, frames);
        stream.set_frames(frames);
    }

    std::string name() const
    {
        return "Zero";
    }
};

//-----------------------------------------------------------------------------

// some helper functions
template<class FrameType, class SampleType, class ByteType, class ChannelType>
inline FrameType bytes_in_frames(ByteType bytes, ChannelType channels)
{
    if (channels == 0)
    {
        return 0;
    }
    return boost::numeric_cast<FrameType>(bytes / sizeof(SampleType) / channels);
}

template<class SampleType, class FrameType, class ChannelType>
inline size_t frames_in_bytes(FrameType frames, ChannelType channels)
{
    return boost::numeric_cast<size_t>(frames * sizeof(SampleType) * channels);
}

template<class ReturnType> // unsigned min
inline ReturnType unsigned_min(uint64_t value0, uint64_t value1)
{
    return static_cast<ReturnType>(value0 < value1 ? value0 : value1);
}

#endif // _AUDIO_STREAM_H_
