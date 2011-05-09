/*
*   applejuice music player
*   this is beerware! you are strongly encouraged to invite the authors of
*   this software to a beer if you happen to run into them.
*   also, this code is licensed under teh GPL, i guess. whatever.
*   copyright 'n shit: year MMXI by maep
*/

#ifndef _LADSPAHOST_H_
#define _LADSPAHOST_H_

#include <boost/scoped_ptr.hpp>

#include "audiostream.h"

//-----------------------------------------------------------------------------

class LadspaHost : public Machine
{
public:
    static size_t const APPEND = static_cast<size_t>(-1);

    LadspaHost();

    virtual ~LadspaHost();

    void process(AudioStream& stream, const uint32_t frames);

    std::string name() const
    {
        return "LADSPA Host";
    }

private:

    struct Pimpl;
    boost::scoped_ptr<Pimpl> pimpl;
};

#endif // _LADSPAHOST_H_
