#include <cstring>
#include <vector>

#include <boost/numeric/conversion/cast.hpp>

#include "libsamplerate/samplerate.h"

#include "logror.h"
#include "convert.h"

using namespace std;
using namespace logror;

struct Resample::Pimpl
{
	void Free();
	void UpdateChannels(uint32_t channels);
	void Reset();
	double ratio;
	AudioStream inStream;
	vector<SRC_STATE*> states;
	Pimpl() : ratio(1) {}
};

Resample::Resample() : pimpl(new Pimpl) {}

Resample::~Resample()
{
	pimpl->Free();
}

void Resample::Pimpl::UpdateChannels(uint32_t channels)
{
	if (states.size() > channels)
		for (size_t i = channels; i < states.size(); ++i)
			src_delete(states[i]);

	if (states.size() < channels)
	{
		for (size_t i = states.size(); i < channels; ++i)
		{
			LogDebug("new resampler %1%"), i;
			int err = 0;
			SRC_STATE* state = src_new(SRC_SINC_FASTEST, 1, &err);
			if (err)
				Log(warning, "src_new error: %1%"), src_strerror(err);
			else
				states.push_back(state);
		}
		Reset();
	}
}

void Resample::Process(AudioStream & stream, uint32_t const frames)
{
	if (pimpl->ratio == 1)
		return source->Process(stream, frames);

	AudioStream & inStream = pimpl->inStream;
	uint32_t const inFrames = (frames / pimpl->ratio) + .5;
	source->Process(inStream, inFrames);

	pimpl->UpdateChannels(inStream.Channels());
	stream.SetChannels(inStream.Channels());
	if (stream.MaxFrames() < frames)
		stream.Resize(frames);

	SRC_DATA data;
	data.src_ratio = pimpl->ratio;
	data.input_frames = inStream.Frames();
	data.output_frames = frames;
	data.end_of_input = inStream.endOfStream ? 1 : 0;

	for (size_t i = 0; i < pimpl->states.size(); ++i)
	{
   		data.data_in = inStream.Buffer(i);
   		data.data_out = stream.Buffer(i);
		int const err = src_process(pimpl->states[i], &data);
		if (err)
			Error("src_process error: %1%"), src_strerror(err);
	}
	// if output contins wanted frames there might be more
	stream.endOfStream = inStream.endOfStream
		&& data.output_frames_gen != data.output_frames;
	stream.SetFrames(data.output_frames_gen);
	if(stream.endOfStream) LogDebug("eos resample %1% frames left"), stream.Frames();
}

void Resample::Pimpl::Reset()
{
	for (size_t i = 0; i < states.size(); ++i)
	{
		int err = src_reset(states[i]);
		if (err)
			Log(warning, "src_reset error: %1%"), src_strerror(err);
	}
}

void Resample::Set(uint32_t sourceRate, uint32_t outRate)
{
	pimpl->ratio = static_cast<double>(outRate) / sourceRate;
	pimpl->Reset();
}

void Resample::Pimpl::Free()
{
	for (size_t i = 0; i < states.size(); ++i)
		src_delete(states[i]);
	states.clear();
}

// src wrappers

void FloatToInt16(float const * in, int16_t* out, uint32_t len)
{
	src_float_to_short_array (in, out, boost::numeric_cast<int>(len));
}

