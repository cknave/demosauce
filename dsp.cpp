#include <cmath>
#include <cstring>
#include <vector>
#include <limits>
#include <algorithm>

#include<boost/static_assert.hpp>

#include "logror.h"
#include "dsp.h"

using namespace logror;

double DbToAmp(double db)
{
	return pow(10, db / 20);
}

double AmpToDb(double amp)
{
	return log10(amp) * 20;
}

//-----------------------------------------------------------------------------
typedef std::vector<Machine::MachinePtr> MachinePtrVector;

struct MachineStack::Pimpl
{
	MachinePtrVector machines;
};

MachineStack::MachineStack() : pimpl(new Pimpl) {}
MachineStack::~MachineStack() {} // needed or scoped_ptr may start whining

void MachineStack::Process(AudioStream& stream, uint32_t const frames)
{
	if (!source.get())
		return;
	return source->Process(stream, frames);
}

void MachineStack::AddMachine(MachinePtr& machine, size_t position)
{
	static size_t const maxMachines = 1000;
	if (machine.get() == this)
		return;
	MachinePtrVector & machines = pimpl->machines;
	if (position == add && machines.size() < maxMachines)
		machines.push_back(machine);
	else if (position <= maxMachines)
	{
		if (machines.size() < position + 1)
			machines.resize(position + 1);
		machines[position] = machine;
	}
}

void MachineStack::RemoveMachine(MachinePtr& machine)
{
	MachinePtrVector & machines = pimpl->machines;
	size_t lastMachine = 0;
	for (size_t i = 0; i < machines.size(); i++)
	{
		if (machines[i] == machine)
			machines[i] = MachinePtr(); // other way to do this?
		if (machines[i].get())
			lastMachine = i;
	}
	machines.resize(lastMachine + 1);
}

void MachineStack::UpdateRouting()
{
	MachinePtr sourceMachine;
	MachinePtrVector & machines = pimpl->machines;
	size_t i = 0;
	for(; i < machines.size() && !sourceMachine.get(); ++i)
		if (machines[i].get() && machines[i]->Enabled())
			sourceMachine = machines[i];
	for(; i < machines.size(); ++i)
		if (machines[i].get() && machines[i]->Enabled())
		{
			LogDebug("connect %1% -> %2%"), sourceMachine->Name(), machines[i]->Name();
			machines[i]->SetSource(sourceMachine);
			sourceMachine = machines[i];
		}
	SetSource(sourceMachine);
}

//-----------------------------------------------------------------------------
void MapChannels::Process(AudioStream& stream, uint32_t const frames)
{
	source->Process(stream, frames);
	uint32_t const inChannels = stream.Channels();
	stream.SetChannels(outChannels);

	if (inChannels == 1 && outChannels == 2)
		memcpy(stream.Buffer(1), stream.Buffer(0), stream.ChannelBytes());

	else if (inChannels == 2 && outChannels == 1)
	{
		float * left = stream.Buffer(0);
		float const * right = stream.Buffer(1);
		for (uint_fast32_t i = stream.Frames(); i; --i)
		{
			float const value = (*left + *right) * .5;
			*left++ = value;
			++right;
		}
	}
}

//-----------------------------------------------------------------------------
void LinearFade::Set(uint64_t startFrame, uint64_t endFrame, float beginAmp, float endAmp)
{
	if (startFrame >= endFrame || beginAmp < 0 || endAmp < 0)
		return;
	this->startFrame = startFrame;
	this->endFrame = endFrame;
	this->currentFrame = 0;
	this->amp = beginAmp;
	this->ampInc = (endAmp - beginAmp) / (endFrame - startFrame);
}

void LinearFade::Process(AudioStream& stream, uint32_t const frames)
{
	source->Process(stream, frames);
	uint32_t const readFrames = stream.Frames();
	uint32_t const endA = (startFrame < currentFrame) ? 0 :
		unsigned_min<uint32_t>(readFrames, startFrame - currentFrame);
	uint32_t const endB = (endFrame < currentFrame) ? 0 :
		unsigned_min<uint32_t>(readFrames, endFrame - currentFrame);
	currentFrame += readFrames;
	if (amp == 1 && (endA >= readFrames || endB == 0))
		return; // nothing to do; amp mignt not be exacly on target, so proximity check would be better

	for (uint_fast32_t iChan = 0; iChan < stream.Channels(); ++iChan)
	{
		float * out = stream.Buffer(iChan);
		uint_fast32_t i = 0;
		float a = amp;
		for (; i < endA; ++i)
			*out++ *= a;
		for (; i < endB; ++i)
			{ *out++ *= a; a += ampInc;	}
		for (; i < readFrames; ++i)
			*out++ *= a;
	}
	amp += ampInc * (endB - endA);
}

//-----------------------------------------------------------------------------
void Gain::Process(AudioStream& stream, uint32_t const frames)
{
	source->Process(stream, frames);
	for (uint_fast32_t iChan = 0; iChan < stream.Channels(); ++iChan)
	{
		float* out = stream.Buffer(iChan);
		for (uint_fast32_t i = stream.Frames(); i; --i)
			*out++ *= amp;
	}
}

//-----------------------------------------------------------------------------
void NoiseSource::SetDuration(uint64_t duration)
{
	this->duration = duration;
	currentFrame = 0;
}

void NoiseSource::Process(AudioStream& stream, uint32_t const frames)
{
	if (currentFrame >= duration)
	{
		stream.SetFrames(0);
		stream.endOfStream = true;
		return;
	}
	if (stream.MaxFrames() < frames)
		stream.Resize(frames);
	float const gah = 1.0 / RAND_MAX;
	const uint_fast32_t procFrames = unsigned_min<uint32_t>(duration - currentFrame, frames);

	for (uint_fast32_t iChan = 0; iChan < stream.Channels(); ++iChan)
	{
		float* out = stream.Buffer(iChan);
		for (uint_fast32_t i = procFrames; i; --i)
			*out++ = gah * rand();
	}

	currentFrame += procFrames;
	stream.endOfStream = currentFrame >= duration;
	stream.SetFrames(procFrames);
}

//-----------------------------------------------------------------------------
void MixChannels::Set(float llAmp, float lrAmp, float rrAmp, float rlAmp)
{
	this->llAmp = llAmp;
	this->lrAmp = lrAmp;
	this->rrAmp = rrAmp;
	this->rlAmp = rlAmp;
}

void MixChannels::Process(AudioStream& stream, uint32_t const frames)
{
	source->Process(stream, frames);
	if (stream.Channels() != 2)
		return;
	float* left = stream.Buffer(0);
	float* right = stream.Buffer(1);
	for (uint_fast32_t i = stream.Frames(); i; --i)
	{
		float const newLeft = llAmp * *left + lrAmp * *right;
		float const newRight = rrAmp * *right + rlAmp * *left;
		*left++ = newLeft;
		*right++ = newRight;
	}
}

//-----------------------------------------------------------------------------
// this needs to be rewritten
/*
Brickwall::Brickwall() :
	attackLength(441), // 1 ms at 44100
	releaseLength(441)
{
	Reset();
}

void Brickwall::Set(uint32_t attackFrames, uint32_t releaseFrames)
{
	attackLength = attackFrames;
	releaseLength = releaseFrames;
}

void Brickwall::Reset()
{
	peak = 1;
	gain = 1;
	gainInc = 0;
	streamPos = 0;
	attackEnd = 0;
	sustainEnd = 0;
	releaseEnd = 0;
	inStream.SetFrames(0);
	buffStream.SetFrames(0);
}

// the peaks buffer is filles with the peaks of both channels
void ProcessPeaks(AudioStream& stream, AlignedBuffer<float>& peaks)
{
	if (peaks.Size() < stream.Frames())
		peaks.Resize(stream.Frames());
	float* out = peaks.Get();
	float const* in0 = stream.Buffer(0);
	if (stream.Channels() == 2)
	{
		float const* in1 = stream.Buffer(1);
		for (uint_fast32_t i = stream.Frames(); i; --i)
			*out++ = std::max(fabs(*in0++), fabs(*in1++));
	}
	else if (stream.Channels() == 1)
		for (uint_fast32_t i = stream.Frames(); i; --i)
		*out++ = fabs(*in0++);
	//TODO implement for 3+ channels
}

void Brickwall::Process(AudioStream& stream, uint32_t const frames)
{
	source->Process(inStream, frames);
	// prepare data for peak scanning
	stream.endOfStream = inStream.endOfStream && inStream.Frames() + buffStream.Frames() < frames;
	stream.SetFrames(0);
	if (buffStream.Frames() == attackLength)
		stream.Append(buffStream);

	uint32_t streamFrames = !stream.endOfStream ?
		std::min(inStream.Frames(), inStream.Frames() - attackLength) :
		std::min(inStream.Frames(), frames);

	stream.Append(inStream, streamFrames);
	inStream.Drop(streamFrames);
	buffStream.SetFrames(0);
	buffStream.Append(inStream);
	
	if (streamPos < attackLength)
	{
		ProcessPreaks(stream, peakBuffer);
	}
		

	{
		ProcessPeaks(stream, peakBuffer);
		if (ampBuffer.Size() < stream.Frames)
			ampBuffer.Resize(stream.Frames);

		float* amp = ampBuffer.Get();
		float* in = PeakBuffer.Get();

		for (uint_fast32_t i = stream.Frames(); i; --i, ++streamPos)
		{
			float const value = *in++;
			if (value > peak)
			{
				peak = value;
				gainInc = (1 / peak - gain) / attackLength;
				attackEnd = streamPos + attackLength;
			}

			if (value > 1)
			{
				sustainEnd = streamPos + attackLength;
				releaseEnd = 0;
			}

			if (streamPos == attackEnd)
				gainInc = 0;

			if (streamPos == sustainEnd)
			{
				gainInc = (1 - gain) / releaseLength;
				releaseEnd = streamPos + releaseLength;
				peak = 1;
			}

			if (streamPos == releaseEnd)
			{
				gain = 1;
				gainInc = 0;
			}

			*amp++ = gain;
			gain += gainInc;
		}
	}


	{
		float const* amp = ampBuffer.Get();
		float* out = fooStream.Buffer(iChan);
		for (uint32_t iChan = 0; iChan < stream1.Channels(); ++iChan)
			for (uint_fast32_t i = fooStream.Frames(); i; --i)
				*out++ *= *amp++;
	}

	if (stream.endOfStream)
		Reset();
	if(stream.endOfStream)
		LogDebug("eos brick %1% frames left"), stream.Frames();
}
*/
//-----------------------------------------------------------------------------
void Peaky::Process(AudioStream& stream, uint32_t const frames)
{
	source->Process(stream, frames);
	float max = peak;
	for (uint_fast32_t iChan = 0; iChan < stream.Channels(); ++iChan)
	{
		float const * in = stream.Buffer(iChan);
		for (uint_fast32_t i = stream.Frames(); i; --i)
		{
			float const value = fabs(*in++);
			if (value > max)
				max = value;
		}
	}
	peak = max;
}

