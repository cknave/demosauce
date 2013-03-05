/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "util.h"

#ifndef NO_OVERRUN_ASSERT
    #define OVERRUN_ASSERT(buf) assert(*(uint32_t*)((char*)(buf)->buff + (buf)->size) == MAGIC_NUMBER)
#else
    #define OVERRUN_ASSERT(buf)
#endif

void* util_malloc(size_t size)
{
    void* ptr = NULL;
    int r = posix_memalign(&ptr, MEM_ALIGN, size);
    return r ? NULL : ptr;
}    

void* util_realloc(void* ptr, size_t size)
{
    if (ptr) {
        ptr = realloc(ptr, size);
        if ((size_t)ptr % MEM_ALIGN != 0) {
            void* tmp_ptr = util_malloc(size);
            memmove(tmp_ptr, ptr, size);
            free(ptr);
            ptr = tmp_ptr;
        }
    } else {
        ptr = util_malloc(size);
    }
    return ptr;
}

//-----------------------------------------------------------------------------

const char* keyval_str(const char* heap, const char* key, const char* fallback)
{
    const char* tmp = strstr(heap, key);
    if (!tmp)
        return strdup(fallback);
    tmp += strlen(key);
    tmp = strpbrk(tmp, "=\n\r");
    if (!tmp || *tmp != '=')
        return strdup(fallback);
    tmp += strspn(tmp + 1, " \t");
    size_t span = strcspn("\n\r");
    while (span && isspace(s[span - 1]))
        span--;
    const char* value = util_malloc(span);
    memmove(value, s, span);
    value[span] = 0;
    return value;
}

int keyval_int(const char* heap, const char* key, int fallback)
{
    const char* tmp = strstr(heap, key);
    if (!tmp)
        return fallback;
    tmp += strlen(key);
    tmp = strpbrk(tmp, "=\n\r");
    if (!tmp || *tmp != '=')
        return fallback;
    tmp += strspn(tmp + 1, " \t");
    int val = 0;
    if (sscanf(tmp, "%d", &val) != 1)
        return fallback;
    return value;
}
   
//-----------------------------------------------------------------------------

int socket_open(const char* host, int port)
{
    int fd = -1;
    char portstr[10] = {0};
    struct addrinfo* info = NULL;
    struct addrinfo hints = {0};
    
    if (snprintf(portstr, sizeof(portstr), "%d", port) < 0)
        return -1;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &info))
        return -1;

    for (struct addrinfo* i = info; i; i = i->ai_next) {
        fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, info->ai_addr, info->ai_addrlen) < 0)
           close(fd);
        else
            break;
    }

    freeaddrinfo(info);
    return fd;
}

bool socket_read(int socket, struct buffer* buf)
{
    ssize_t bytes = 0;
    char buff[4096];
    if (send(socket, "NEXTSONG", 8, 0) == -1)
        return false;
    bytes = recv(socket, buff, sizeof(buff) - 1, 0);
    if (bytes < 0)
       return false;
    if (buf->size < size + 1)
        buffer_resize(buf, size + 1);
    memmove(buf->buff, buff, bytes);
    buf->buff[size] = 0;
}

void socket_close(int socket)
{
    close(socket);
}

//-----------------------------------------------------------------------------

void buffer_resize(struct buffer* buf, size_t size) 
{
    OVERRUN_ASSERT(buf);
    buf->size = size;
    buf->buff = util_realloc(buf->buff, buf->size + sizeof(uint32_t));
    *(uint32_t*)((char*)buf->buff + size) = MAGIC_NUMBER;
}

void buffer_zero(struct buffer* buf)
{
    OVERRUN_ASSERT(buf);
    memset(buf->buff, 0, buf->size);
}

void buffer_zero_end(struct buffer* buf, size_t size)
{
    OVERRUN_ASSERT(buf);
    size_t start = buf->size - MIN(size, buf->size);
    memset(buf->buff + start, 0, buf->size - start);
}

//-----------------------------------------------------------------------------

void stream_init(struct stream* s, int channels)
{
    assert(channels > 1 && channels <= MAX_CHANNELS);
    memset(s, 0, sizeof(struct stream));
    s->channels = channels;
}

void stream_free(struct stream* s)
{
    for (int i = 0; i < s->channels; i++)
        free(s->buff[i].buff);
}

void stream_resize(struct stream* s, int frames)
{
    for (int i = 0; i < s->channels; i++)
        OVERRUN_ASSERT(s->buff[i]);
    if (s->max_frames == frames) 
        return;
    s->max_frames = frames;
    for (int i = 0; i < s->channels; i++) 
        buffer_resize(&s->buff[i], frames * sizeof(float));
}

float* stream_buffer(struct stream* s, int channel)
{
    for (int i = 0; i < s->channels; i++)
        OVERRUN_ASSERT(&s->buff[i]);
    return s->buff[channel].buff;
}

void stream_set_channels(struct stream* s, int channels)
{
    assert(channels > 1 && channels <= MAX_CHANNELS);
    for (int i = 0; i < s->channels; i++)
        OVERRUN_ASSERT(&s->buff[i]);
    if (channels != s->channels) {
        s->channels = channels;
        stream_resize(s, s->max_frames);
    }
}

void stream_set_frames(struct stream* s, int frames)
{
    assert(frames <= s->max_frames);
    for (int i = 0; i < s->channels; i++)
        OVERRUN_ASSERT(&s->buff[i]);
    s->frames = frames;
}

void stream_append_n(struct stream* s, struct stream* source, int frames)
{
    for (int i = 0; i < s->channels; i++)
        OVERRUN_ASSERT(&s->buff[i]);
    frames = MIN(frames, source->frames);
    if (s->frames + frames > s->max_frames)
        stream_resize(s, s->frames + source->frames);
    for (int i = 0; i < s->channels && i < source->channels; i++)
        memmove(s->buff[i].buff + s->frames, source->buff[i].buff, frames * sizeof(float));
    s->frames += frames;
}

void stream_append(struct stream* s, struct stream* source)
{
    stream_append_s(s, source, source->frames);
}

void stream_drop(struct stream* s, int frames)
{
    assert(frames <= s->frames);
    for (int i = 0; i < s->channels; i++)
        OVERRUN_ASSERT(&s->buff[i]);
    int remaining_frames = s->frames - frames;
    if (remaining_frames > 0)
        for (int i = 0; i < s->channels; i++)
            memmove(s->buff[i].buff, s->buff[i].buff + frames * sizeof(frames), remaining_frames * sizeof(float));
    s->frames = remaining_frames;
}

void stream_zero(struct stream* s, int offset, int frames)
{
    assert(offset + frames <= s->max_frames);
    for (int i = 0; i < s->channels; i++)
        OVERRUN_ASSERT(&s->buff[i]);
    for (int i = 0; i < s->channels; i++)
        memset(s->buff[i] + offset * sizeof(float), 0, s->frames * sizeof(float));
}

