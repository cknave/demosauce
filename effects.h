/*
*   applejuice music player
*   this is beerware! you are strongly encouraged to invite the authors of
*   this software to a beer if you happen to run into them.
*   also, this code is licensed under teh GPL, i guess. whatever.
*   copyright 'n shit: year MMX by maep
*/

#ifndef _EFFECTS_H_
#define _EFFECTS_H_

#include <boost/scoped_ptr.hpp>

#include "audiostream.h"

// db conversion functions
double db_to_amp(double db);
double amp_to_db(double amp);

//-----------------------------------------------------------------------------

class MachineStack : public Machine
{
public:
    static size_t const APPEND = static_cast<size_t>(-1);

    MachineStack();

    virtual ~MachineStack();

    void process(AudioStream & stream, uint32_t const frames);

    std::string name() const
    {
        return "Machine Stack";
    }

    template<typename T> void add(T& machine, size_t position = APPEND);

    template<typename T> void remove(T& machine);

    void update_routing();

private:
    void add_machine(MachinePtr& machine, size_t position);
    void remove_machine(MachinePtr& machine);

    struct Pimpl;
    boost::scoped_ptr<Pimpl> pimpl;
};

template<typename T>
inline void MachineStack::add(T& machine, size_t position)
{
    MachinePtr base_machine = boost::static_pointer_cast<Machine>(machine);
    add_machine(base_machine, position);
}

template<typename T>
inline void MachineStack::remove(T& machine)
{
    MachinePtr base_machine = boost::static_pointer_cast<Machine>(machine);
    remove_machine(base_machine);
}

//-----------------------------------------------------------------------------

class MapChannels : public Machine
{
public:
    MapChannels() :
        out_channels(2)
    {}

    void process(AudioStream& stream, uint32_t const frames);

    std::string name() const
    {
        return "Map Channels";
    }

    uint32_t channels() const
    {
        return out_channels;
    }

    void set_channels(uint32_t channels)
    {
        out_channels = channels == 1 ? 1 : 2;
    }

private:
    uint32_t out_channels;
};

//-----------------------------------------------------------------------------

class LinearFade : public Machine
{
public:
    void process(AudioStream& stream, uint32_t const frames);

    std::string name() const
    {
        return "Linear Fade";
    }

    void set_fade(uint64_t start_frame, uint64_t end_frame, float begin_amp, float end_amp);

private:
    uint64_t start_frame;
    uint64_t end_frame;
    uint64_t current_frame;
    double amp;
    double amp_inc;
};

//-----------------------------------------------------------------------------

class Gain : public Machine
{
public:
    Gain() :
        amp(1)
    {}

    void process(AudioStream & stream, uint32_t const frames);

    std::string name() const
    {
        return "Gain";
    }

    void set_amp(float amp)
    {
        if (amp >= 0)
            this->amp = amp;
    }

private:
    float amp;
};

//-----------------------------------------------------------------------------

class NoiseSource : public Machine
{
public:
    NoiseSource() :
        channels(2),
        duration(0),
        current_frame(0)
    {}

    void process(AudioStream & stream, uint32_t const frames);

    std::string name() const
    {
        return "Noise";
    }

    void set_duration(uint64_t duration);

    void set_channels(uint32_t channels)
    {
        this->channels = channels;
    }

private:
    uint32_t channels;
    uint64_t duration;
    uint64_t current_frame;
};

//-----------------------------------------------------------------------------

class MixChannels : public Machine
{
public:
    MixChannels() :
        ll_amp(1),
        lr_amp(0),
        rr_amp(1),
        rl_amp(0)
    {}

    void process(AudioStream & stream, uint32_t const frames);

    std::string name() const
    {
        return "Mix Channels";
    }

    // left = left*llAmp + left*lrAmp; rigt = right*rrAmp + left*rlAmp;
    void set_mix(float ll_amp, float lr_amp, float rr_amp, float rl_amp);

private:
    float ll_amp;
    float lr_amp;
    float rr_amp;
    float rl_amp;
};

//-----------------------------------------------------------------------------

class Peaky : public Machine
{
public:
    Peaky() :
        _peak(0)
    {}

    // overwriting
    void process(AudioStream & stream, uint32_t const frames);
    std::string name() const
    {
        return "Peaky";
    }

    void reset()
    {
        _peak = 0;
    }

    float peak() const
    {
        return _peak;
    }

private:
    float _peak;
};

#endif // _EFFECTS_H_
