/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#ifndef FFDECODER_H
#define FFDECODER_H

#include <stdbool.h>
#include "util.h"

bool    ff_probe(const char* filename);
void*   ff_load(const char* file_name);
void    ff_free(void* handle);
void    ff_decode(void* handle, struct stream* s, int frames);
void    ff_seek(void* handle, long frame);
void    ff_info(void* handle, struct info* info);
char*   ff_metadata(void* handle, const char* key);

#endif // FFDECODER_H

