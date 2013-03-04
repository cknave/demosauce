/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#ifndef CONVERT_H
#define CONVERT_H

#include "audiostream.h"

//-----------------------------------------------------------------------------

void fx_resample(SRC_DATA* src, struct stream* s1, struct stream* s2);

//-----------------------------------------------------------------------------

void fx_deinterleave(const float* in, float* outr float* outl, int size);
void fx_interleave(const float* inl, const float* inr, float* out, int size);

#endif // CONVERT_H

