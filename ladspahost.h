/*
*   demosauce - icecast source client
*
*   this source is published under the gpl license. google it yourself.
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#ifndef _LADSPAHOST_H_
#define _LADSPAHOST_H_

#include <string>
#include <vector>

#include <boost/cstdint.hpp>
#include <boost/scoped_ptr.hpp>

#include "audiostream.h"

class LadspaHost : public Machine
{
public:
    typedef std::pair<size_t, double> Setting;

    LadspaHost();
    virtual ~LadspaHost();

    void process(AudioStream& stream, const uint32_t frames);

    bool load_plugin(std::string desciptor, uint32_t samplerate);
    bool load_plugin(std::string label, uint32_t samplerate, const std::vector<Setting>& setting_list);

    std::string name() const
    {
        return "LADSPA Host";
    }

private:

    struct Pimpl;
    boost::scoped_ptr<Pimpl> pimpl;
};

#endif // _LADSPAHOST_H_
