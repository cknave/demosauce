/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <cstring>
#include <vector>

#include <boost/numeric/conversion/cast.hpp>

#include <samplerate.h>

#include "logror.h"
#include "convert.h"

struct Resample::Pimpl
{
    void free();
    void update_channels(uint32_t channels);
    void reset();
    double ratio;
    AudioStream in_stream;
    std::vector<SRC_STATE*> states;
};

Resample::Resample() :
    pimpl(new Pimpl)
{
    pimpl->ratio = 1;
}

Resample::~Resample()
{
    pimpl->free();
}

void Resample::Pimpl::update_channels(uint32_t channels)
{
    if (states.size() == channels)
    {
        return;
    }

    free();

    for (size_t i = states.size(); i < channels; ++i)
    {
        LOG_DEBUG("[resample] new channel %1%"), i;
        int err = 0;
        SRC_STATE* state = src_new(SRC_SINC_FASTEST, 1, &err);
        if (err)
        {
            LOG_WARNING("[resample] src_new error: %1%"), src_strerror(err);
        }
        else
        {
            states.push_back(state);
        }
    }
}

void Resample::process(AudioStream& stream, uint32_t const frames)
{
    if (pimpl->ratio == 1)
    {
        return source->process(stream, frames);
    }

    AudioStream& in_stream = pimpl->in_stream;
    uint32_t const in_frames = (frames / pimpl->ratio) + .5;
    source->process(in_stream, in_frames);

    pimpl->update_channels(in_stream.channels());
    stream.set_channels(in_stream.channels());
    if (stream.max_frames() < frames)
    {
        stream.resize(frames);
    }

    SRC_DATA data;
    data.src_ratio = pimpl->ratio;
    data.input_frames = in_stream.frames();
    data.output_frames = frames;
    data.end_of_input = in_stream.end_of_stream ? 1 : 0;

    for (size_t i = 0; i < pimpl->states.size(); i++)
    {
        data.data_in = in_stream.buffer(i);
        data.data_out = stream.buffer(i);
        int const err = src_process(pimpl->states[i], &data);
        if (err)
        {
            ERROR("[resample] src_process error: %1%"), src_strerror(err);
        }
    }

    // if output contins wanted frames there might be more
    stream.end_of_stream = in_stream.end_of_stream
        && data.output_frames_gen != data.output_frames;
    stream.set_frames(data.output_frames_gen);

    if(stream.end_of_stream)
    {
        LOG_DEBUG("[resample] eos %1% frames left"), stream.frames();
    }
}

void Resample::set_rates(uint32_t sourceRate, uint32_t outRate)
{
    pimpl->free();
    pimpl->ratio = static_cast<double>(outRate) / sourceRate;
}

void Resample::Pimpl::free()
{
    for (size_t i = 0; i < states.size(); ++i)
    {
        src_delete(states[i]);
    }
    states.clear();
}

std::string Resample::name() const
{
    return "Resample";
}

// src wrappers

void float_to_int16(const float* in, int16_t* out, uint32_t len)
{
    src_float_to_short_array(in, out, boost::numeric_cast<int>(len));
}

