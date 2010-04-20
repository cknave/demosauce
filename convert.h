#ifndef _H_CONVERT_
#define _H_CONVERT_

#include <cassert>
#include <limits>
#include <boost/utility.hpp>
#include <boost/shared_ptr.hpp>

#include "dsp.h"

//-----------------------------------------------------------------------------

class Resample : public Machine
{
public:
	Resample();
	virtual ~Resample();
	// overwriting
	void Process(AudioStream & stream, uint32_t const frames);
	std::string Name() const { return "Resample"; }

	void Set(uint32_t sourceRate, uint32_t outRate);
private:
	struct Pimpl;
	boost::scoped_ptr<Pimpl> pimpl;
};

//-----------------------------------------------------------------------------

template <typename SampleType>
class ConvertFromInterleaved : boost::noncopyable
{
public:

	ConvertFromInterleaved() : channels(0) {}

	SampleType* Buffer(uint32_t frames, uint32_t channels)
	{
		this->channels = channels;
		if (buffer.Size() < frames * channels)
			buffer.Resize(frames * channels);
		return buffer.Get();
	}

	void Process(AudioStream & stream, uint32_t const frames);

private:
	uint32_t channels;
	AlignedBuffer<SampleType> buffer;
};

template<> inline void ConvertFromInterleaved<int16_t>
::Process(AudioStream & stream, uint32_t const frames)
{
	assert(buffer.Size() >= frames * channels);

	stream.SetChannels(channels);
	if (stream.MaxFrames() < frames)
		stream.Resize(frames);
	stream.SetFrames(frames);

	static float const range = 1.0 / -std::numeric_limits<int16_t>::min(); //2's complement
	for (uint_fast32_t iChan = 0; iChan < channels; ++iChan)
	{
		int16_t const * in = buffer.Get() + iChan;
		float* out = stream.Buffer(iChan);
		for (uint_fast32_t i = frames; i; --i)
		{
			*out++ = range * *in;
			in += channels;
		}
	}
}

template<> inline void ConvertFromInterleaved<float>
::Process(AudioStream & stream, uint32_t const frames)
{
	assert(buffer.Size() >= frames * channels);

	stream.SetChannels(channels);
	if (stream.MaxFrames() < frames)
		stream.Resize(frames);
	stream.SetFrames(frames);

	for (uint_fast32_t iChan = 0; iChan < channels; ++iChan)
	{
		float const * in = buffer.Get() + iChan;
		float* out = stream.Buffer(iChan);
		for (uint_fast32_t i = frames; i; --i)
		{
			*out++ = *in;
			in += channels;
		}
	}
}

//-----------------------------------------------------------------------------
void FloatToInt16(float const * in, int16_t* out, uint32_t len);

class ConvertToInterleaved
{
public:

	template <typename MachineType>
	void SetSource(MachineType & machine)
	{
		source = boost::static_pointer_cast<Machine>(machine);
	}

	template <typename SampleType>
	uint32_t Process(SampleType* outSamples, uint32_t frames, uint32_t channels);
	template <typename SampleType>
	uint32_t Process1(SampleType* outSamples, uint32_t frames);
	template <typename SampleType>
	uint32_t Process2(SampleType* outSamples, uint32_t frames);

private:
	Machine::MachinePtr source;
	AlignedBuffer<int16_t> convertBuffer;
	AudioStream inStream;
};

template<typename SampleType> inline uint32_t ConvertToInterleaved
::Process(SampleType* outSamples, uint32_t frames, uint32_t channels)
{
	assert(channels > 0 && channels <= 2);
	if (channels == 2)
		return Process2(outSamples, frames);
	return Process1(outSamples, frames);
}

template<> inline uint32_t ConvertToInterleaved
::Process1(int16_t* outSamples, uint32_t frames)
{
	uint32_t procFrames = 0;

	while (procFrames < frames)
	{
		source->Process(inStream, frames - procFrames);
		assert(inStream.Channels() == 1);
		FloatToInt16(inStream.Buffer(0), outSamples, inStream.Frames());
		outSamples += inStream.Frames();
		procFrames += inStream.Frames();
		assert(procFrames <= frames);
		if (inStream.endOfStream)
			break;
	}
	return procFrames;
}

template<> inline uint32_t ConvertToInterleaved
::Process2(int16_t* outSamples, uint32_t frames)
{
	uint32_t procFrames = 0;
	int16_t* out = outSamples;

	while (procFrames < frames)
	{
		source->Process(inStream, frames - procFrames);
		assert(inStream.Channels() == 2);
		uint32_t const inFrames = inStream.Frames();

		if (convertBuffer.Size() < inFrames * 2)
			convertBuffer.Resize(inFrames * 2);
		int16_t* buff0 = convertBuffer.Get();
		int16_t* buff1 = buff0 + inFrames;

		FloatToInt16(inStream.Buffer(0), buff0, inFrames);
		FloatToInt16(inStream.Buffer(1), buff1, inFrames);

		for (uint_fast32_t i = inFrames; i; --i)
		{
			*out++ = *buff0++;
			*out++ = *buff1++;
		}

		procFrames += inFrames;
		assert(procFrames <= frames);
		if (inStream.endOfStream)
			break;
	}
	return procFrames;
}

#endif
