/*
*   libReplayGain, based on mp3gain 1.5.1
*   LGPL 2.1
*   http://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
*/

#ifndef _H_REPLAY_GAIN_
#define _H_REPLAY_GAIN_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>

#ifndef BOOL
#define BOOL int
#endif

#ifndef FALSE
#define FALSE 0
#define TRUE (!FALSE)
#endif

// #define RG_UNSIGNED_8_BIT 00
#define RG_SIGNED_16_BIT 1
#define RG_SIGNED_32_BIT 2
#define RG_FLOAT_32_BIT 3
#define RG_FLOAT_64_BIT 4

typedef struct
{
	uint32_t sampleRate;
	uint32_t sampleType;
	uint32_t numberChannels;
	BOOL interleaved;
} RG_SampleFormat;

typedef struct _RG_CONTEXT RG_Context;

RG_Context* RG_NewContext(RG_SampleFormat* format);
void RG_FreeContext(RG_Context* context);

// data must contain pointer one or more pointers, depending on channels and format
void RG_Analyze(RG_Context* context, void* data, uint32_t frames);

double RG_GetTitleGain(RG_Context* context);
double RG_GetAlbumGain(RG_Context* context);

size_t RG_FormatSize(uint32_t sampleFormat);

#ifdef __cplusplus
}
#endif

#endif
