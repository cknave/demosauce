/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#ifndef EFFECTS_H
#define EFFECTS_H

#include "audiostream.h"

// db conversion functions
double db_to_amp(double db);
double amp_to_db(double amp);

//-----------------------------------------------------------------------------

void fx_map(struct stream* s, int channels);

//-----------------------------------------------------------------------------

struct fx_fade {
    long    start_frame;
    long    end_frame;
    long    current_frame;
    double  amp;
    double  amp_inc;
};

void fx_fade_init(struct fx_fade* fx)
void fx_fade_process(struct fx_fade* fx, struct stream* s);

//-----------------------------------------------------------------------------

// left = left*llAmp + left*lrAmp; rigt = right*rrAmp + left*rlAmp;

struct fx_mix {
    float ll_amp;
    float lr_amp;
    float rr_amp;
    float rl_amp;
};

void fx_mix(struct fx_mix* fx, struct stream* s);

#endif // EFFECTS_H

