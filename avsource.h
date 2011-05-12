/*
*   demosauce - icecast source client
*
*   this source is published under the gpl license. google it yourself.
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#ifndef _AVSOURCE_H_
#define _AVSOURCE_H_

#include <string>

#include <boost/cstdint.hpp>
#include <boost/scoped_ptr.hpp>

#include "audiostream.h"

class AvSource : public Decoder
{
public:
    AvSource();
    virtual ~AvSource();
    static bool probe_name(std::string url);

    // manipulators from AbstractSource
    bool load(std::string file_name);
    void process(AudioStream& stream, uint32_t frames);
    void seek(uint64_t frame);

    // observers from AbstractSource
    std::string name() const;
    uint32_t channels() const;
    uint32_t samplerate() const;
    uint64_t length() const;
    float bitrate() const;
    bool seekable() const;
    std::string metadata(std::string key) const;

private:
    struct Pimpl;
    boost::scoped_ptr<Pimpl> pimpl;
};

#endif
