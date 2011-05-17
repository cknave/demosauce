/*
*   demosauce - fancy icecast source client
*
*   this source is published under the gpl license. google it yourself.
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <cstdlib>
#include <sstream>
#include <iostream>

#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>

#include "logror.h"
#include "ladspahost.h"

using std::cout;
using std::endl;
using std::vector;
using std::string;

namespace fs = ::boost::filesystem;

void list_plugins()
{
    char* ladspa_path = getenv("LADSPA_PATH");
    if (ladspa_path == 0)
    {
        std::cerr << "warning: the LADSPA_PATH environment variable is not set\n";
    }
    vector<string> list;
    ladspa_enumerate_plugins(list);
    BOOST_FOREACH(string path, list)
    {
        vector<const LADSPA_Descriptor*> desc_list;
        LadspaHandle h = ladspa_load(path, desc_list);

        if (h != 0 && !desc_list.empty())
        {
            BOOST_FOREACH(const LADSPA_Descriptor* d, desc_list)
            {
                cout << path << " " <<
                    d->Label << " " <<
                    d->Name << endl;
            }
        }
        ladspa_unload(h);
    }
}

void list_ports(string plugin)
{
    vector<string> list;
    ladspa_enumerate_plugins(list);

    LadspaHandle handle = 0;
    const LADSPA_Descriptor* desc = 0;

    BOOST_FOREACH(string path, list)
    {
        vector<const LADSPA_Descriptor*> desc_list;
        handle = ladspa_load(path, desc_list);
        if (handle != 0 && !desc_list.empty())
        {
            BOOST_FOREACH(const LADSPA_Descriptor* d, desc_list)
            {
                if (plugin == d->Label)
                {
                    desc = d;
                    break;
                }
            }
        }
        if (desc != 0)
        {
            break;
        }
        ladspa_unload(handle);
    }

    if (desc == 0)
    {
        cout << "no plugin with that label\n";
        return;
    }

    std::stringstream conf_str;
    conf_str << "ladspa_pluginX = " << desc->Label;
    for (unsigned long i = 0; i < desc->PortCount; i++)
    {
        cout << i << ": " << desc->PortNames[i];
        if (LADSPA_IS_PORT_INPUT(desc->PortDescriptors[i])
            && LADSPA_IS_PORT_CONTROL(desc->PortDescriptors[i]))
        {
            LADSPA_Data value = ladspa_default_value(desc->PortRangeHints[i]);
            cout << " min:" << desc->PortRangeHints[i].LowerBound
                << " max:" << desc->PortRangeHints[i].UpperBound
                << " default:" << value;
            conf_str << " " << i << ":" << value;
        }
        else
        {
            cout << " (no control input)";
        }
        cout << endl;
    }
    cout << "example for .conf: "<< conf_str.str() << endl;
    ladspa_unload(handle);
}

int main(int argc, char* argv[])
{
    log_set_file_level(logror::nothing);

    if (argc == 1)
    {
        list_plugins();
    }
    else if (argc == 2)
    {
        list_ports(argv[1]);
    }
    else
    {
        cout << "ladspainfo 0.1\nsyntax: ladspainfo - lists all plugins\n"
            "        ladspainfo <plugin label> - list ports of a plugin\n";
    }

    return EXIT_SUCCESS;
}

