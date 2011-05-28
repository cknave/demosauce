/*
*   libReplayGain, based on mp3gain 1.5.1
*   LGPL 2.1
*   http://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
*/

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "replay_gain.h"
#include "gain_analysis.h"

struct _RG_CONTEXT
{
	RG_SampleFormat format;
	Context_t * cxt;
	void * buffer;
	size_t bufferSize;
};

RG_Context* RG_NewContext(RG_SampleFormat* format)
{
	if (format->numberChannels != 1 && format->numberChannels != 2)
		return NULL;
	Context_t* cxt = NewAnalyzeContext();
	int val = InitGainAnalysis(cxt, format->sampleRate);
	if (val == INIT_GAIN_ANALYSIS_ERROR)
	{
		FreeAnalyzeContext(cxt);
		return NULL;
	}
	RG_Context* context = malloc(sizeof(RG_Context));
	memset(context, 0, sizeof(RG_Context));
	context->format = *format;
	context->cxt = cxt;
	context->buffer = NULL;
	context->bufferSize = 0;
	return context;
}

void RG_FreeContext(RG_Context * context)
{
	FreeAnalyzeContext(context->cxt);
	free(context->buffer);
}

size_t RG_FormatSize(uint32_t sampleFormat)
{
	switch (sampleFormat)
	{
		case RG_SIGNED_16_BIT:
            return sizeof(int16_t);
		case RG_SIGNED_32_BIT:
            return sizeof(int32_t);
		case RG_FLOAT_32_BIT:
            return sizeof(float);
		case RG_FLOAT_64_BIT:
            return sizeof(double);
		default:
            return 0;
	}
}

void UpdateBuffer(RG_Context* context, uint32_t frames)
{
	size_t requiredSize = frames * sizeof(Float_t) * context->format.numberChannels;
	if (context->bufferSize < requiredSize)
		context->buffer = realloc(context->buffer, requiredSize);
}

void ConvertF64(RG_Context* context, void* data, uint32_t length)
{
	static Float_t const buttScratcher = 0x7fff;
	double** buffer = (double**) data;
	if (context->format.numberChannels == 2 && context->format.interleaved)
	{
		double const* in = buffer[0];
		Float_t* outl = (Float_t*) context->buffer;
		Float_t* outr = (Float_t*) context->buffer + context->bufferSize / 2;
		for (size_t i = 0; i < length; i++)
		{
			*outl++ = (Float_t) *in++ * buttScratcher;
			*outr++ = (Float_t) *in++ * buttScratcher;
		}
	}
	else
	{
		Float_t* out = (Float_t*) context->buffer;
		for (uint32_t iChan = 0; iChan < context->format.numberChannels; ++iChan)
		{
			double const* in = buffer[iChan];
			for (size_t i = 0; i < length; i++)
				*out++ = (Float_t) *in++ * buttScratcher;
		}
	}
}

void ConvertF32(RG_Context* context, void* data, uint32_t length)
{
	static Float_t const buttScratcher = 0x7fff;
	float** buffer = (float**) data;
	if (context->format.numberChannels == 2 && context->format.interleaved)
	{
		float const * in = buffer[0];
		Float_t * outl = (Float_t *) context->buffer;
		Float_t * outr = (Float_t *) context->buffer + context->bufferSize / 2;
		for (size_t i = 0; i < length; i++)
		{
			*outl++ = (Float_t) *in++ * buttScratcher;
			*outr++ = (Float_t) *in++ * buttScratcher;
		}
	}
	else
	{
		Float_t* out = (Float_t*) context->buffer;
		for (uint32_t iChan = 0; iChan < context->format.numberChannels; ++iChan)
		{
			float const* in = buffer[iChan];
			for (size_t i = 0; i < length; i++)
				*out++ = (Float_t) *in++ * buttScratcher;
		}
	}
}

void ConvertS32(RG_Context* context, void* data, uint32_t length)
{
	static Float_t const buttScratcher = 0x7fff / 0x7fffffff;
	int32_t** buffer = (int32_t**) data;
	if (context->format.numberChannels == 2 && context->format.interleaved)
	{
		int32_t const *  in = buffer[0];
		Float_t * outl = (Float_t *) context->buffer;
		Float_t * outr = (Float_t *) context->buffer + context->bufferSize / 2;
		for (size_t i = 0; i < length; i++)
		{
			*outl++ = (Float_t) *in++ * buttScratcher;
			*outr++ = (Float_t) *in++ * buttScratcher;
		}
	}
	else
	{
		Float_t* out = (Float_t*) context->buffer;
		for (uint32_t iChan = 0; iChan < context->format.numberChannels; ++iChan)
		{
			int32_t const * in = buffer[iChan];
			for (size_t i = 0; i < length; i++)
				*out++ = (Float_t) *in++ * buttScratcher;
		}
	}
}

void ConvertS16(RG_Context* context, void* data, uint32_t length)
{
	int16_t** buffer = (int16_t**) data;
	if (context->format.numberChannels == 2 && context->format.interleaved)
	{
		int16_t const *  in = (int16_t*) data;
		Float_t* outl = (Float_t*) context->buffer;
		Float_t* outr = (Float_t*) context->buffer + context->bufferSize / 2;
		for (size_t i = 0; i < length; i++)
		{
			*outl++ = (Float_t) *in++;
			*outr++ = (Float_t) *in++;
		}
	}
	else
	{
		Float_t* out = (Float_t*) context->buffer;
		for (uint32_t iChan = 0; iChan < context->format.numberChannels; ++iChan)
		{
			int16_t const * in = buffer[iChan];
			for (size_t i = 0; i < length; i++)
				*out++ = (Float_t) *in++;
		}
	}
}

void RG_Analyze(RG_Context* context, void* data, uint32_t frames)
{
	assert(context);
	assert(data);
    // having odd number of frames sometimes causes odd behaviour
    assert(frames % 2 == 0);

	UpdateBuffer(context, frames);
	switch (context->format.sampleType)
	{
		case RG_SIGNED_16_BIT:
            ConvertS16(context, data, frames);
            break;
		case RG_SIGNED_32_BIT:
            ConvertS32(context, data, frames);
            break;
		case RG_FLOAT_32_BIT:
            ConvertF32(context, data, frames);
            break;
		case RG_FLOAT_64_BIT:
            ConvertF64(context, data, frames);
            break;
		default:
            return;
	}

	if (context->format.numberChannels == 2)
	{
		void* leftBuffer = context->buffer;
		void* rightBuffer = (float*)leftBuffer + frames;
		AnalyzeSamples(context->cxt, leftBuffer, rightBuffer, frames, 2);
	}
	else if (context->format.numberChannels == 1)
		AnalyzeSamples(context->cxt, context->buffer, NULL, frames, 1);
}

double RG_GetTitleGain(RG_Context* context)
{
	double gain = GetTitleGain(context->cxt);
	if (gain == GAIN_NOT_ENOUGH_SAMPLES)
		return 0; // no change
	return gain;
}

double RG_GetAlbumGain(RG_Context* context)
{
	double gain = GetAlbumGain(context->cxt);
	if (gain == GAIN_NOT_ENOUGH_SAMPLES)
		return 0; // no change
	return gain;
}
