/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
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

#include <ladspa.h>

#include "audiostream.h"

// some static helper functions
typedef void* LadspaHandle;
void            ladspa_enumerate_plugins(std::vector<std::string>& list);
LadspaHandle    ladspa_load(std::string path, std::vector<const LADSPA_Descriptor*>& desc);
void            ladspa_unload(LadspaHandle handle);
LADSPA_Data     ladspa_default_value(LADSPA_PortRangeHint hint);

class LadspaHost : public Machine
{
public:
    typedef std::pair<size_t, LADSPA_Data> Setting;

    LadspaHost();
    virtual ~LadspaHost();

    void process(AudioStream& stream, uint32_t frames);

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
