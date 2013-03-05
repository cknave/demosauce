/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*
*/

#ifndef AUDIOSTREAM_H
#define AUDIOSTREAM_H

#include <stdint.h>
#include <assert.h>
#include "util.h"

#ifndef NO_OVERRUN_ASSERT
    #define OVERRUN_ASSERT(buf) assert(*(uint32_t*)((char*)(buf)->buff + (buf)->size) == MAGIC_NUMBER)
#else
    #define OVERRUN_ASSERT(buf)
#endif

static const int MAX_CHANNELS = 2;
static const uint32_t MAGIC_NUMBER = 0xaa55aa55;

//-----------------------------------------------------------------------------

struct buffer {
    void*   buff;
    size_t  size;
};
    
inline void buffer_resize(struct buffer* buf, size_t size) 
{
    OVERRUN_ASSERT(buf);
    buf->size = size;
    buf->buff = aligned_realloc(buf->buff, buf->size + sizeof(uint32_t));
    *(uint32_t*)((char*)buf->buff + size) = MAGIC_NUMBER;
}

inline void buffer_zero(struct buffer* buf)
{
    OVERRUN_ASSERT(buf);
    memset(buf->buff, 0, buf->size);
}

inline void buffer_zero_end(struct buffer* buf, size_t size)
{
    OVERRUN_ASSERT(buf);
    size_t start = buf->size - MIN(size, buf->size);
    memset(buf->buff + start, 0, buf->size - start);
}

//-----------------------------------------------------------------------------

struct stream
{
    struct  buffer buff[MAX_CHANNELS];
    int     channels;
    long    frames;
    long    max_frames;
    bool    end_of_stream;
};

inline void stream_init(struct stream* s, int channels)
{
    assert(channels > 1 && channels <= MAX_CHANNELS);
    memset(s, 0, sizeof(struct stream));
    s->channels = channels;
}

inline void stream_free(struct stream* s)
{
    for (int i = 0; i < s->channels; i++)
        free(s->buff[i].buff);
}

inline void stream_resize(struct stream* s, int frames)
{
    for (int i = 0; i < s->channels; i++)
        OVERRUN_ASSERT(s->buff[i]);
    if (s->max_frames == frames) 
        return;
    s->max_frames = frames;
    for (int i = 0; i < s->channels; i++) 
        buffer_resize(&s->buff[i], frames * sizeof(float));
}

inline float* stream_buffer(struct stream* s, int channel)
{
    for (int i = 0; i < s->channels; i++)
        OVERRUN_ASSERT(&s->buff[i]);
    return s->buff[channel].buff;
}

inline void stream_set_channels(struct stream* s, int channels)
{
    assert(channels > 1 && channels <= MAX_CHANNELS);
    for (int i = 0; i < s->channels; i++)
        OVERRUN_ASSERT(&s->buff[i]);
    if (channels != s->channels) {
        s->channels = channels;
        stream_resize(s, s->max_frames);
    }
}

inline void stream_set_frames(struct stream* s, int frames)
{
    assert(frames <= s->max_frames);
    for (int i = 0; i < s->channels; i++)
        OVERRUN_ASSERT(&s->buff[i]);
    s->frames = frames;
}

inline void stream_append_n(struct stream* s, struct stream* source, int frames)
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

inline void stream_append(struct stream* s, struct stream* source)
{
    stream_append_s(s, source, source->frames);
}

inline void stream_drop(struct stream* s, int frames)
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

inline void void stream_zero(struct stream* s, int offset, int frames)
{
    assert(offset + frames <= s->max_frames);
    for (int i = 0; i < s->channels; i++)
        OVERRUN_ASSERT(&s->buff[i]);
    for (int i = 0; i < s->channels; i++)
        memset(s->buff[i] + offset * sizeof(float), 0, s->frames * sizeof(float));
}

//-----------------------------------------------------------------------------

struct decoder
{
    bool        (*load)         (struct decoder* d, const char* file);
    void        (*seek)         (struct decoder* d, long frame);
    long        (*length)       (struct decoder* d);
    int         (*channels)     (struct decoder* d);
    int         (*samplerate)   (struct decoder* d);    // must never return 0
    bool        (*seekable)     (struct decoder* d);
    const char* (*metadata)     (struct decoder* d);    // metadata keys: codec_type, artist, title, album
};

//-----------------------------------------------------------------------------

#endif // AUDIOSTREAM_H

