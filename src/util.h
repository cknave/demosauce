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
#define MAGIC_NUMBER    0xaa55aa55
#define MEM_ALIGN       16
#define BSTR(buf) ((char*)(buf).buff)
#define XSTR_(s) "-"#s
#define XSTR(s) XSTR_(s)
#ifndef BUILD_ID
    #define BUILD_ID
#endif
#define DEMOSAUCE_VERSION "demosauce 0.4.0"XSTR(BUILD_ID)" - C++ is to C as Lung Cancer is to Lung"
#define COUNT(array) (sizeof(array) / sizeof(array[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct buffer {
    void*           buff;
    size_t          size;
};

struct stream {
    struct buffer   buff[MAX_CHANNELS];
    int             channels;
    long            frames;
    long            max_frames;
    bool            end_of_stream;
};

struct info {
    int             channels;
    int             samplerate;
    long            length;
    float           bitrate;
    const char*     codec;
    bool            seekable;
};


void*   util_malloc(size_t size);
void*   util_realloc(void* ptr, size_t size);
void    util_free(void* ptr);


char*   util_strdup(const char* str);
char*   util_trim(char* str);


int     socket_open(const char* host, int port);
void    socket_read(int socket, struct buffer* buf);
void    socket_close(int socket);


char*   keyval_str(char* buf, int size, const char* str, const char* key, const char* fallback);
int     keyval_int(const char* str, const char* key, int fallback);
double  keyval_real(const char* str, const char* key, double fallback);
bool    keyval_bool(const char* str, const char* key, bool fallback);
    

void    buffer_resize(struct buffer* buf, size_t size);
void    buffer_zero(struct buffer* buf);
void    buffer_zero_end(struct buffer* buf, size_t size);


void    stream_init(struct stream* s, int channels);
void    stream_free(struct stream* s);
void    stream_resize(struct stream* s, int frames);
float*  stream_buffer(struct stream* s, int channel);
void    stream_set_channels(struct stream* s, int channels);
void    stream_set_frames(struct stream* s, int frames);
void    stream_append_n(struct stream* s, struct stream* source, int frames);
void    stream_append(struct stream* s, struct stream* source);
void    stream_drop(struct stream* s, int frames);
void    stream_zero(struct stream* s, int offset, int frames);

#endif // UTIL_H

