#ifndef _H_DSP_
#define _H_DSP_

#include <cassert>
#include <string>
#include <boost/cstdint.hpp>
#include <boost/utility.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include "mem.h"

#ifndef NO_OVERRUN_ASSERT
	#define OVERRUN_ASSERT(arg) assert(arg)
#else
	#define OVERRUN_ASSERT(arg)
#endif

//-----------------------------------------------------------------------------
template <typename T>
class AlignedBuffer : boost::noncopyable
{
public:
	AlignedBuffer() : buffer(0), size(0) {}

	AlignedBuffer(size_t const size) : buffer(0), size(size)
	{
		Resize(size);
	}

	~AlignedBuffer()
	{
		OVERRUN_ASSERT(NoOverrun());
		free(buffer);
	}

	T* Resize(size_t const size)
	{
		OVERRUN_ASSERT(NoOverrun());
		buffer = aligned_realloc(buffer, size * sizeof(T) + sizeof(uint32_t));
		this->size = size;
		*reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(buffer)
			+ size * sizeof(T)) = magicNumber;
		return reinterpret_cast<T*>(buffer);
	}

	T* Get() const
	{
		OVERRUN_ASSERT(NoOverrun());
		return reinterpret_cast<T*>(buffer);
	}

	size_t Size() const
	{
		OVERRUN_ASSERT(NoOverrun());
		return size;
	}

	void Zero()
	{
		OVERRUN_ASSERT(NoOverrun());
		memset(buffer, 0, size * sizeof(T));
	}

	bool NoOverrun() const
	{
		if (!buffer)
			return true;
		return *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(buffer)
			+ size * sizeof(T)) == magicNumber;
	}

private:
	static uint32_t const magicNumber = 0xaaAAaaAA;
	void* buffer;
	size_t size;
};

//-----------------------------------------------------------------------------
// currently only supports max two channels
class AudioStream : boost::noncopyable
{
public:
	AudioStream() :
		endOfStream(false),
		frames(0),
		maxFrames(0),
		channels(2)
	{
		buffer[0] = 0;
		buffer[1] = 0;
	}

	virtual ~AudioStream()
	{
		OVERRUN_ASSERT(NoOverrun());
		free(buffer[0]);
		free(buffer[1]);
	}

	void Resize(uint32_t frames)
	{
		OVERRUN_ASSERT(NoOverrun());
		if (maxFrames == frames)
			return;
		maxFrames = frames;
		size_t bufferSize = sizeof(float) * (frames + 1);
		for (uint32_t i = 0; i < channels; ++i)
		{
			void* buf = aligned_realloc(buffer[i], bufferSize);
			buffer[i] = reinterpret_cast<float*>(buf);
			buffer[i][maxFrames] = magicNumber;
		}
	}

	float* Buffer(uint32_t channel) const
	{
		OVERRUN_ASSERT(NoOverrun());
		return buffer[channel];
	}

	uint8_t* BytifiedBuffer(uint32_t channel) const
	{
		return reinterpret_cast<uint8_t*>(Buffer(channel));
	}

	uint32_t Channels() const
	{
		return channels;
	}

	uint32_t Frames() const
	{
		OVERRUN_ASSERT(NoOverrun());
		return frames;
	}

	uint32_t MaxFrames() const
	{
		return maxFrames;
	}

	void SetChannels(uint32_t const channels)
	{
		assert(channels == 1 || channels == 2);
		if (channels != this->channels)
		{
			this->channels = channels;
			Resize(maxFrames);
		}
	}

	void SetFrames(uint32_t frames)
	{
		OVERRUN_ASSERT(NoOverrun());
		assert(frames <= maxFrames);
		this->frames = frames;
	}

	size_t ChannelBytes() const
	{
		OVERRUN_ASSERT(NoOverrun());
		return frames * sizeof(float);
	}

	bool NoOverrun() const
	{
		for (uint32_t i = 0; i < channels; ++i)
			if (buffer[i] && buffer[i][maxFrames] != magicNumber)
				return false;
		return true;
	}

	void Append(AudioStream & stream)
	{
		OVERRUN_ASSERT(NoOverrun());
		if (frames + stream.Frames() > maxFrames)
			Resize(frames + stream.Frames());
		for (uint32_t i = 0; i < channels && i < stream.Channels(); ++i)
			memcpy(buffer[i] + frames, stream.Buffer(i), stream.ChannelBytes());
		frames += stream.Frames();
	}

	void Append(AudioStream& stream, uint32_t frames)
	{
		OVERRUN_ASSERT(NoOverrun());
		frames = std::min(frames, stream.Frames());
		if (this->frames + frames > maxFrames)
			Resize(this->frames + stream.Frames());
		size_t channelBytes = frames * sizeof(float);
		for (uint32_t i = 0; i < channels && i < stream.Channels(); ++i)
			memcpy(buffer[i] + this->frames, stream.Buffer(i), channelBytes);
		this->frames += frames;
	}

	void Drop(uint32_t frames)
	{
		OVERRUN_ASSERT(NoOverrun());
		assert(frames <= this->frames);
		uint32_t remainingFrames = this->frames - frames;
		if (remainingFrames > 0)
			for (uint32_t i = 0; i < channels; ++i)
				memmove(buffer[i], buffer[i] + frames, remainingFrames * sizeof(float));
		this->frames = remainingFrames;
	}

	void Zero(uint32_t frames)
	{
		OVERRUN_ASSERT(NoOverrun());
		assert(frames <= this->frames);
		for (uint32_t iChan = 0; iChan < channels; ++iChan)
			memset(buffer[iChan], 0, frames * sizeof(float));
		this->frames = frames;
	}

	bool endOfStream;

private:
	uint32_t frames;
	uint32_t maxFrames;
	uint32_t channels;
	float* buffer[2]; // meh i'd rather have arbitrary number of chans
	static float const magicNumber = 567.89;
};

//-----------------------------------------------------------------------------
class Machine : boost::noncopyable
{
public:
	Machine() : enabled(true) {}
	typedef boost::shared_ptr<Machine> MachinePtr;

	virtual void Process(AudioStream& stream, uint32_t const frames) = 0;
	virtual std::string Name() const = 0;

	void SetSource(MachinePtr& machine)
	{
		if (machine.get() != this)
			source = machine;
	}

	void SetEnabled(bool enabled)
	{
		this->enabled = enabled;
	}

 	bool Enabled() const
 	{
 		return enabled;
 	}

protected:
	MachinePtr source;
	bool enabled;
};

//-----------------------------------------------------------------------------
class MachineStack : public Machine
{
public:
	static size_t const add = static_cast<size_t>(-1);
	MachineStack();
	virtual ~MachineStack();

	// overwriting
	void Process(AudioStream & stream, uint32_t const frames);
	std::string Name() const { return "Machine Stack"; }

	template<typename T> void AddMachine(T & machine, size_t position = add);
	template<typename T> void RemoveMachine(T & machine);
	void UpdateRouting();
private:
	void AddMachine(MachinePtr & machine, size_t position);
	void RemoveMachine(MachinePtr & machine);
	struct Pimpl;
	boost::scoped_ptr<Pimpl> pimpl;
};

template<typename T>
inline void MachineStack::AddMachine (T & machine, size_t position)
{
	MachinePtr baseMachine = boost::static_pointer_cast<Machine>(machine);
	AddMachine(baseMachine, position);
}

template<typename T>
inline void MachineStack::RemoveMachine(T & machine)
{
	MachinePtr baseMachine = boost::static_pointer_cast<Machine>(machine);
	RemoveMachine(baseMachine);
}

//-----------------------------------------------------------------------------
class MapChannels : public Machine
{
public:
	MapChannels() : outChannels(2) {}

	// overwriting
	void Process(AudioStream & stream, uint32_t const frames);
	std::string Name() const { return "Map Channels"; }
	uint32_t Channels() const { return outChannels; }

	void SetOutChannels(uint32_t channels) { outChannels = channels == 1 ? 1 : 2; }

private:
	uint32_t outChannels;
};

//-----------------------------------------------------------------------------
class LinearFade : public Machine
{
public:
	// overwriting
	void Process(AudioStream & stream, uint32_t const frames);
	std::string Name() const { return "Linear Fade"; }

	void Set(uint64_t startFrame, uint64_t endFrame, float beginAmp, float endAmp);
private:
	uint64_t startFrame;
	uint64_t endFrame;
	uint64_t currentFrame;
	double amp;
	double ampInc;
};

//-----------------------------------------------------------------------------
class Gain : public Machine
{
public:
	Gain() : amp(1) {}

	// overwriting
	void Process(AudioStream & stream, uint32_t const frames);
	std::string Name() const { return "Gain"; }

	void SetAmp(float amp) { if (amp >= 0) this->amp = amp; }
private:
	float amp;
};

//-----------------------------------------------------------------------------
class NoiseSource : public Machine
{
public:
	NoiseSource() : channels(2), duration(0), currentFrame(0) {}

	// overwriting
	void Process(AudioStream & stream, uint32_t const frames);
	std::string Name() const { return "Noise"; }

	void SetDuration(uint64_t duration);
	void SetChannels(uint32_t channels) { this->channels = channels; }

private:
	uint32_t channels;
	uint64_t duration;
	uint64_t currentFrame;
};

//-----------------------------------------------------------------------------
class DummySource : public Machine
{
public:
	// overwriting
	void Process(AudioStream & stream, uint32_t const frames) { stream.Zero(frames); }
	std::string Name() const { return "Dummy"; }
};

//-----------------------------------------------------------------------------
class MixChannels : public Machine
{
public:
	MixChannels() : llAmp(1), lrAmp(0), rrAmp(1), rlAmp(0) {}

	// overwriting
	void Process(AudioStream & stream, uint32_t const frames);
	std::string Name() const { return "Mix Channels"; }

	// left = left*llAmp + left*lrAmp; rigt = right*rrAmp + left*rlAmp;
	void Set(float llAmp, float lrAmp, float rrAmp, float rlAmp);
private:
	float llAmp;
	float lrAmp;
	float rrAmp;
	float rlAmp;
};

//-----------------------------------------------------------------------------
// needs to be rewritten
/* 
class Brickwall : public Machine
{
public:
	Brickwall();
	virtual ~Brickwall() {}

	// overwriting
	void Process(AudioStream & stream, uint32_t const frames);
	std::string Name() const { return "Brickwall"; }

	void Set(uint32_t attackFrames, uint32_t releaseFrames);
private:
	void Reset();
	AudioStream inStream;
	AudioStream buffStream;
	AlignedBuffer<float> ampBuffer;
	AlignedBuffer<float> peakBuffer;
	float peak;
	float gain;
	float gainInc;
	uint32_t attackLength;
	uint32_t releaseLength;
	uint64_t streamPos;
	uint64_t attackEnd;
	uint64_t sustainEnd;
	uint64_t releaseEnd;
};
*/
//-----------------------------------------------------------------------------
class Peaky : public Machine
{
public:
	Peaky() : peak(0) {}

	// overwriting
	void Process(AudioStream & stream, uint32_t const frames);
	std::string Name() const { return "Peaky"; }

	void Reset() { peak = 0; }
	float Peak() const { return peak; }

private:
	float peak;
};

//-----------------------------------------------------------------------------

// some helper functions
double DbToAmp(double db);
double AmpToDb(double amp);

template<typename FrameType, typename SampleType, typename ByteType, typename ChannelType>
inline FrameType BytesInFrames(ByteType const bytes, ChannelType const channels)
{
	if (channels == 0)
		return 0;
	return boost::numeric_cast<FrameType>(bytes / sizeof(SampleType) / channels);
}

template<typename SampleType, typename FrameType, typename ChannelType>
inline size_t FramesInBytes(FrameType const frames, ChannelType const channels)
{
	return boost::numeric_cast<size_t>(frames * sizeof(SampleType) * channels);
}

template<typename ReturnType> // unsigned min
inline ReturnType unsigned_min(uint64_t const value0, uint64_t const value1)
{
	return static_cast<ReturnType>(value0 < value1 ? value0 : value1);
}

#endif
