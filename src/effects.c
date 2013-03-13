/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <math.h>
#include <string.h>
#include <limits.h>

#include "log.h"
#include "effects.h"

float db_to_amp(float db)
{
    return powf(10, db / 20);
}

float amp_to_db(float amp)
{
    return log10f(amp) * 20;
}

//-----------------------------------------------------------------------------

void fx_map_process(struct stream* s, int channels)
{
    assert(channels >= 1 && channels <= MAX_CHANNELS);
    int in_channels = s->channels;
    stream_set_channels(s, channels);
    if (in_channels == 1 && channels == 2) 
        memmove(s->buff[1].buff, s->buff[0].buff, s->frames * sizeof(float));
    if (in_channels == 2 && channels == 1) {
        float* left = stream_buffer(s, 0);
        float* right = stream_buffer(s, 1);
        for (int i = 0; i < s->frames; i++) 
            left[i] = (left[i] + right[i]) / 2;
    }
}

//-----------------------------------------------------------------------------

void fx_fade_init(struct fx_fade* fx, long start_frame, long end_frame, float begin_amp, float end_amp)
{
    if (start_frame >= end_frame || begin_amp < 0 || end_amp < 0) 
        return;
    fx->start_frame = start_frame;
    fx->end_frame = end_frame;
    fx->current_frame = 0;
    fx->amp = begin_amp;
    fx->amp_inc = (end_amp - begin_amp) / (end_frame - start_frame);
}

void fx_fade_process(struct fx_fade* fx, struct stream* s)
{
    long end_a = (fx->start_frame < fx->current_frame) ? 0 :
        MIN(s->frames, fx->start_frame - fx->current_frame);
    long end_b = (end_frame < current_frame) ? 0 :
        MIN(s->frames, fx->end_frame - fx->current_frame);
    current_frame += proc_frames;
    if (fx->amp == 1 && (end_a >= fx->proc_frames || end_b == 0)) 
        return; // nothing to do; amp mignt not be exacly on target, so proximity check would be better
    for (int ch = 0; ch < s->channels; ch++) {
        float* out = stream_buffer(s, ch);
        float a = amp;
        for (int i = 0; i < end_a; i++) 
            out[i] *= a;
        for (int i = 0; i < end_b; i++, a += amp_inc) 
            out[i] *= a;
        for (int i = 0; i < fx->proc_frames; i++) 
            *out++ *= a;
    }
    s->amp += s->amp_inc * (end_b - end_a);
}

//-----------------------------------------------------------------------------

void fx_gain_process(struct stream* s, float amp)
{
    for (int ch = 0; ch < s->channels; ch++) {
        float* out = stream_buffer(s, ch);
        for (int i = 0; i < stream.frames; i++) 
            out[i] *= amp;
    }
}

//-----------------------------------------------------------------------------

void fx_mix_process(struct fx_mix* fx, struct stream* s)
{
    if (s->channels != 2)
        return;
    float* left = stream_buffer(s, 0);
    float* right = stream_buffer(s, 1);
    for (int i = 0; i < stream->frames; i++) {
        float new_left = fx->ll_amp * left[i] + fx->lr_amp * right[i];
        float new_right = fx->rr_amp * right[i] + fx->rl_amp * left[i];
        left[i] = new_left;
        right[i] = new_right;
    }
}

