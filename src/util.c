/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#define _POSIX_C_SOURCE 200112L

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include "util.h"
#include "log.h"
#include "effects.h"

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

void util_free(void* ptr)
{
    free(ptr);
}

//-----------------------------------------------------------------------------

bool util_isfile(const char* path)
{
    struct stat buf = {0};
    int err = stat(path, &buf);
    return !err && S_ISREG(buf.st_mode);
}

long util_filesize(const char* path)
{
    struct stat buf = {0};
    int err = stat(path, &buf);
    return err ? -1 : buf.st_size;
}

//-----------------------------------------------------------------------------

char* util_strdup(const char* str)
{
    if (!str)
        return NULL;
    char* s = util_malloc(strlen(str) + 1);
    strcpy(s, str);
    return s;
}

char* util_trim(char* str)
{
    char* tmp = str;
    while (isspace(*str))
        str++;
    memmove(str, tmp, strlen(tmp));
    tmp += strlen(tmp) - 1;
    while (tmp > str && isspace(*tmp))
        tmp--;
    *tmp = 0;
    return str;
}

char* keyval_str(char* out, int size, const char* heap, const char* key, const char* fallback)
{
    const char* tmp = strstr(heap, key);
    if (!tmp)
        goto error; 
    tmp += strlen(key);
    tmp = strpbrk(tmp, "=\n\r");
    if (!tmp || *tmp != '=')
        goto error;
    tmp += strspn(tmp + 1, " \t");
    size_t span = strcspn(tmp, "\n\r");
    while (span && isspace(tmp[span - 1]))
        span--;

    if (out && span >= size) {
        LOG_DEBUG("[keyval_str] buffer too small for value (%s)", key);
        goto error;
    }
    char* value = out ? out : util_malloc(span + 1);
    memmove(value, tmp, span);
    value[span] = 0;
    return value;

error:
    if (!out && fallback) {
        return util_strdup(fallback);
    } else if (out && fallback && strlen(fallback) < size) {
        return strcpy(out, fallback);
    } else if (out && size) {
        LOG_DEBUG("[keyval_str] buffer too small for fallback (%s, %s)", key, fallback);
        return strcpy(out, "");
    } else {
        return NULL;
    }
}

int keyval_int(const char* heap, const char* key, int fallback)
{
    char tmp[16] = {0};
    keyval_str(tmp, 16, heap, key, NULL);
    return strlen(tmp) ? atoi(tmp) : fallback;
}

double keyval_real(const char* heap, const char* key, double fallback)
{
    char tmp[16] = {0};
    keyval_str(tmp, 16, heap, key, NULL);
    return strlen(tmp) ? atof(tmp) : fallback;
}
  
bool keyval_bool(const char* heap, const char* key, bool fallback)
{
    char tmp[8] = {0};
    keyval_str(tmp, 8, heap, key, NULL);
    return strlen(tmp) ? !strcasecmp(tmp, "true") : fallback;
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
    if (getaddrinfo(host, portstr, &hints, &info))
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

bool socket_read(int socket, struct buffer* buffer)
{
    ssize_t bytes = 0;
    char buff[4096];
    if (send(socket, "NEXTSONG", 8, 0) == -1)
        return false;
    bytes = recv(socket, buff, sizeof(buff) - 1, 0);
    if (bytes < 0)
       return false;
    buffer_resize(buffer, bytes + 1);
    memmove(buffer->data, buff, bytes);
    ((char*)buffer->data)[bytes] = 0;
    return true;
}

void socket_close(int socket)
{
    close(socket);
}

//-----------------------------------------------------------------------------

void buffer_resize(struct buffer* buf, size_t size) 
{
    if (buf->size < size) {
        buf->data = util_realloc(buf->data, buf->size);
        buf->size = size;
    }
}

void buffer_zero(struct buffer* buf)
{
    memset(buf->data, 0, buf->size);
}

//-----------------------------------------------------------------------------

void stream_free(struct stream* s)
{
    for (int i = 0; i < MAX_CHANNELS; i++)
        util_free(s->buffer[i]);
}

void stream_resize(struct stream* s, int frames)
{
    assert(s->channels >= 1 && s->channels <= MAX_CHANNELS);
    if (s->max_frames < frames) { 
        for (int ch = 0; ch < s->channels; ch++) 
            util_realloc(s->buffer[ch], frames * sizeof(float));
        s->max_frames = frames;
    }
}

void stream_fill(struct stream* s, const float* source, int frames, int channels)
{
    assert(channels >= 1 && channels <= MAX_CHANNELS);
    if (channels != s->channels)
        s->channels = channels;
    stream_resize(s, frames);
    s->frames = frames;
    if (channels == 1)
        memmove(s->buffer[0], source, frames * sizeof(float));
    else // channels == 2 
        fx_deinterleave(source, s->buffer[0], s->buffer[1], frames);
}

void stream_drop(struct stream* s, int frames)
{
    assert(frames <= s->frames);
    int remaining_frames = s->frames - frames;
    if (remaining_frames > 0)
        for (int i = 0; i < s->channels; i++)
            memmove(s->buffer[i], s->buffer[i] + frames, remaining_frames * sizeof(float));
    s->frames = remaining_frames;
}

void stream_zero(struct stream* s, int offset, int frames)
{
    assert(offset + frames <= s->max_frames);
    for (int i = 0; i < s->channels; i++)
        memset(s->buffer[i] + offset, 0, s->frames * sizeof(float));
    s->frames = offset + frames;
}

