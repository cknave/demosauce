/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <cmath>
#include <cstring>
#include <vector>
#include <limits>
#include <algorithm>

#include<boost/static_assert.hpp>

#include "logror.h"
#include "effects.h"

using namespace logror;

double db_to_amp(double db)
{
    return pow(10, db / 20);
}

double amp_to_db(double amp)
{
    return log10(amp) * 20;
}

//-----------------------------------------------------------------------------

struct MachineStack::Pimpl
{
    std::vector<MachinePtr> machines;
};

MachineStack::MachineStack() :
    pimpl(new Pimpl)
{}

MachineStack::~MachineStack() {} // needed or scoped_ptr may start whining

void MachineStack::process(AudioStream& stream, uint32_t frames)
{
    if (source.get() == 0)
        source = MachinePtr(new ZeroSource);
    return source->process(stream, frames);
}

void MachineStack::add_machine(MachinePtr& machine, size_t position)
{
    static size_t const max_machines = 1000;

    if (machine.get() == this)
    {
        return;
    }

    std::vector<MachinePtr>& machines = pimpl->machines;

    if (position == APPEND && machines.size() < max_machines)
    {
        machines.push_back(machine);
    }
    else if (position < max_machines)
    {
        if (machines.size() < position + 1)
            machines.resize(position + 1);
        machines[position] = machine;
    }
}

void MachineStack::remove_machine(MachinePtr& machine)
{
    std::vector<MachinePtr>& machines = pimpl->machines;
    size_t last_machine = 0;

    for (size_t i = 0; i < machines.size(); i++)
    {
        if (machines[i] == machine)
        {
            machines[i] = MachinePtr(); // other way to do this?
        }
        if (machines[i].get())
        {
            last_machine = i;
        }
    }

    machines.resize(last_machine + 1);
}

void MachineStack::update_routing()
{
    MachinePtr source_machine;
    std::vector<MachinePtr>& machines = pimpl->machines;
    size_t i = 0;

    // find initial machine
    for(; i < machines.size() && !source_machine.get(); ++i)
    {
        if (machines[i].get() && machines[i]->enabled())
        {
            source_machine = machines[i];
        }
    }

    // chain the rest of the machines
    for(; i < machines.size(); ++i)
    {
        if (machines[i].get() && machines[i]->enabled())
        {
            LOG_DEBUG("connect %1% -> %2%"), source_machine->name(), machines[i]->name();
            machines[i]->set_source(source_machine);
            source_machine = machines[i];
        }
    }

    set_source(source_machine);
}

//-----------------------------------------------------------------------------

void MapChannels::process(AudioStream& stream, uint32_t frames)
{
    source->process(stream, frames);
    uint32_t const in_channels = stream.channels();
    stream.set_channels(out_channels);

    if (in_channels == 1 && out_channels == 2)
    {
        memmove(stream.buffer(1), stream.buffer(0), stream.channel_bytes());
    }
    else if (in_channels == 2 && out_channels == 1)
    {
        float* left = stream.buffer(0);
        float const* right = stream.buffer(1);
        for (uint_fast32_t i = stream.frames(); i; --i)
        {
            float const value = (*left + *right) * .5;
            *left++ = value;
            ++right;
        }
    }
}

//-----------------------------------------------------------------------------

void LinearFade::set_fade(uint64_t start_frame, uint64_t end_frame, float begin_amp, float end_amp)
{
    if (start_frame >= end_frame || begin_amp < 0 || end_amp < 0)
    {
        return;
    }
    this->start_frame = start_frame;
    this->end_frame = end_frame;
    this->current_frame = 0;
    this->amp = begin_amp;
    this->amp_inc = (end_amp - begin_amp) / (end_frame - start_frame);
}

void LinearFade::process(AudioStream& stream, uint32_t frames)
{
    source->process(stream, frames);

    uint32_t const proc_frames = stream.frames();

    uint32_t const end_a = (start_frame < current_frame) ? 0 :
        unsigned_min<uint32_t>(proc_frames, start_frame - current_frame);

    uint32_t const end_b = (end_frame < current_frame) ? 0 :
        unsigned_min<uint32_t>(proc_frames, end_frame - current_frame);

    current_frame += proc_frames;

    if (amp == 1 && (end_a >= proc_frames || end_b == 0))
    {
        return; // nothing to do; amp mignt not be exacly on target, so proximity check would be better
    }

    for (uint_fast32_t i_chan = 0; i_chan < stream.channels(); ++i_chan)
    {
        float* out = stream.buffer(i_chan);
        uint_fast32_t i = 0;
        float a = amp;

        for (; i < end_a; ++i)
        {
            *out++ *= a;
        }
        for (; i < end_b; ++i, a += amp_inc)
        {
            *out++ *= a;
        }
        for (; i < proc_frames; ++i)
        {
            *out++ *= a;
        }
    }

    amp += amp_inc * (end_b - end_a);
}

//-----------------------------------------------------------------------------

void Gain::process(AudioStream& stream, uint32_t frames)
{
    source->process(stream, frames);
    for (uint_fast32_t i_chan = 0; i_chan < stream.channels(); ++i_chan)
    {
        float* out = stream.buffer(i_chan);
        for (uint_fast32_t i = stream.frames(); i; --i)
        {
            *out++ *= amp;
        }
    }
}

//-----------------------------------------------------------------------------

void NoiseSource::set_duration(uint64_t duration)
{
    this->duration = duration;
    current_frame = 0;
}

void NoiseSource::process(AudioStream& stream, uint32_t frames)
{
    if (current_frame >= duration)
    {
        stream.set_frames(0);
        stream.end_of_stream = true;
        return;
    }

    if (stream.max_frames() < frames)
    {
        stream.resize(frames);
    }

    float const gah = 1.0 / RAND_MAX;
    const uint_fast32_t proc_frames = unsigned_min<uint32_t>(duration - current_frame, frames);

    for (uint_fast32_t i_chan = 0; i_chan < stream.channels(); ++i_chan)
    {
        float* out = stream.buffer(i_chan);
        for (uint_fast32_t i = proc_frames; i; --i)
        {
            *out++ = gah * rand();
        }
    }

    current_frame += proc_frames;
    stream.end_of_stream = current_frame >= duration;
    stream.set_frames(proc_frames);
}

//-----------------------------------------------------------------------------
void MixChannels::set_mix(float ll_amp, float lr_amp, float rr_amp, float rl_amp)
{
    this->ll_amp = ll_amp;
    this->lr_amp = lr_amp;
    this->rr_amp = rr_amp;
    this->rl_amp = rl_amp;
}

void MixChannels::process(AudioStream& stream, uint32_t frames)
{
    source->process(stream, frames);

    if (stream.channels() != 2)
    {
        return;
    }

    float* left = stream.buffer(0);
    float* right = stream.buffer(1);

    for (uint_fast32_t i = stream.frames(); i; --i)
    {
        float const new_left = ll_amp * *left + lr_amp * *right;
        float const new_right = rr_amp * *right + rl_amp * *left;
        *left++ = new_left;
        *right++ = new_right;
    }
}

//-----------------------------------------------------------------------------

void Peaky::process(AudioStream& stream, uint32_t frames)
{
    source->process(stream, frames);

    for (uint_fast32_t i_chan = 0; i_chan < stream.channels(); ++i_chan)
    {
        float const* in = stream.buffer(i_chan);
        for (uint_fast32_t i = stream.frames(); i; --i)
        {
            float const value = fabs(*in++);
            if (value > _peak )
            {
                _peak  = value;
            }
        }
    }
}

