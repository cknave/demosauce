/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdbool.h>
 
#define MAX_CHANNELS    2
//#define MAGIC_NUMBER    0xaa55aa55

#define INFO_SEEKABLE   1
#define INFO_FFMPEG     (1 << 1)
#define INFO_BASS       (1 << 2)  
#define INFO_MOD        (1 << 16)
#define INFO_AMIGAMOD   (1 << 17)

#define BSTR(buf) ((char*)(buf).buff)
#define XSTR_(s) "-"#s
#define XSTR(s) XSTR_(s)
#ifdef BUILD_ID
    #define ID_STR XSTR(BUILD_ID)
#else
    #define ID_STR
#endif
#define DEMOSAUCE_VERSION "demosauce 0.4.0"ID_STR" - C++ is to C as Lung Cancer is to Lung"
#define COUNT(array) (sizeof(array) / sizeof(array[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(a, b, c) ((b) < (a) ? (a) : (b) > (c) ? (c) : (b))

struct buffer {
    void*           data;
    size_t          size;
};

struct stream {
    float*      buffer[MAX_CHANNELS];
    size_t      buffer_size;
    long        frames;
    long        max_frames;
    int         channels;
    bool        end_of_stream;
};

struct info {
    void        (*decode)(void*, struct stream*, int);
    void        (*free)(void*);
    char*       (*metadata)(void*, const char*);
    const char* codec;
    float       bitrate;
    long        frames;
    int         channels;
    int         samplerate;
    int         flags;
};


void*   util_malloc(size_t size);
void*   util_realloc(void* ptr, size_t size);
void    util_free(void* ptr);


char*   util_strdup(const char* str);
char*   util_trim(char* str);
bool    util_isfile(const char* path);
long    util_filesize(const char* path);

int     socket_open(const char* host, int port);
bool    socket_read(int socket, struct buffer* buffer);
void    socket_close(int socket);


char*   keyval_str(char* buf, int size, const char* str, const char* key, const char* fallback);
int     keyval_int(const char* str, const char* key, int fallback);
double  keyval_real(const char* str, const char* key, double fallback);
bool    keyval_bool(const char* str, const char* key, bool fallback);
    

void    buffer_resize(struct buffer* b, size_t size);
void    buffer_zero(struct buffer* b);


void    stream_free(struct stream* s);
void    stream_resize(struct stream* s, int frames);
void    stream_fill(struct stream* s, const float* source, int frames, int channels);
void    stream_drop(struct stream* s, int frames);
void    stream_zero(struct stream* s, int offset, int frames);

#endif // UTIL_H

