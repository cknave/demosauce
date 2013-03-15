/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#ifndef BASSDEC_H
#define BASSDEC_H

#include "util.h"

void    bass_loadso(char** argv); 
void*   bass_load(const char* path, const char* options);
void    bass_free(void* handle);
void    bass_decode(void* hadle, struct stream* s, int frames);
void    bass_seek(void* handle, long position);
void    bass_set_loop_duration(void* handle, double duration);
void    bass_info(void* handle, struct info* info);
char*   bass_metadata(void* handle, const char* key);
float   bass_loopiness(const char* path);
bool    bass_probe_name(const char* path);

#endif

