/*
*   applejuice music player
*   this is beerware! you are strongly encouraged to invite the authors of
*   this software to a beer if you happen to run into them.
*   also, this code is licensed under teh GPL, i guess. whatever.
*   copyright 'n shit: year MMXI by maep
*/

#include <cstring>
#include <string>
#include <vector>

#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

#include "logror.h"
#include "ladspa.h"

#if defined (__unix__)
    #include <dlfcn.h>
    #define PLUGIN_EXTENSION    ".so"
    #define PATHENV_SEPARATOR   ":"
    #define SEARCH_DIR_0        "/usr/lib/ladspa/"
#elif defined (_W32)
    #include <windows.h>
    #define PLUGIN_EXTENSION    ".dll"
    #define PATHENV_SEPARATOR   ";"
    #error "LADSPA on windows still needs some work, DO IT!"
#else
    #error "LADSPA won't work on your system. disable or implement it"
#endif

using std::string;
using std::vector;

using boost::iends_with;

namespace fs = ::boost::filesystem;

vector<string> enumerate_plugins()
{
    vector<string> list;
    vector<string> search_dirs;

    char* ladspa_path = getenv("LADSPA_PATH");
    if (ladspa_path != 0)
    {
        boost::split(search_dirs, ladspa_path, boost::is_any_of(PATHENV_SEPARATOR));
    }

#ifdef SEARCH_DIR_0
    search_dirs.push_back(SEARCH_DIR_0);
#endif

    BOOST_FOREACH(string dir_name, search_dirs)
    {
        if (!fs::is_directory(dir_name))
        {
            continue;
        }

        for (fs::directory_iterator f = fs::directory_iterator(dir_name); f != fs::directory_iterator(); ++f)
        {
            string file_name = f->string();
            if (fs::is_regular_file(file_name) && iends_with(file_name, PLUGIN_EXTENSION))
            {
                list.push_back(file_name);
            }
        }
    }
    return list;
}


LIB_HANDLE_T dynamic_open(string file_name)
{
#if defined (__unix__)
    return dlopen(file_name.c_str(), RTLD_LAZY);
#else
    #error "implementation missing"
#endif
}

void dynamic_close(LIB_HANDLE_T lib_handle)
{
#if defined (__unix__)
    dlclose(lib_handle);
#else
    #error "implementation missing"
#endif
}

LADSPA_Descriptor_Function bind_descriptor_function(LIB_HANDLE_T lib_handle)
{
#if defined (__unix__)
    return (LADSPA_Descriptor_Function) dlsym(lib_handle, "ladspa_descriptor");
#else
    #error "implementation missing"
#endif
}

struct LadspaHost::Pimpl
{
    Pimpl();
    virtual ~Pimpl();

    bool load_plugin(string label);
    void unload_plugin();
    void init_plugin();

    LIB_HANDLE_T        lib_handle;
    LADSPA_Descriptor*  descriptor;
    LADSPA_Handle       plugin_handle;
};

bool LadspaHost::Pimpl::load_plugin(string label)
{
    unload_plugin();
    vector<string> plugin_list = enumerate_plugins();
    BOOST_FOREACH(string file_name, plugin_list)
    {
        LOG_DEBUG("ladspahost: inspecting %1%"), file_name;

        LIB_HANDLE_T h = dynamic_open(file_name);
        if (h == 0)
        {
            LOG_DEBUG("ladspahost: can't open %1%"), file_name;
            return false;
        }

        LADSPA_Descriptor_Function get_descriptor = bind_descriptor_function(h);
        if (get_descriptor == 0)
        {
            LOG_DEBUG("ladspahost: not a LADSPA plugin %1%"), file_name;
            dynamic_close(h);
            continue;
        }

        for (unsigned long i = 0; true; i++)
        {
            const LADSPA_Descriptor* d = get_descriptor(i);
            if (d == 0)
            {
                break;
            }

            if (label == d->Label)
            {
                LOG_INFO("ladspahost: loading %1% (%2%)"), label, file_name;
                lib_handle = h;
                descriptor = d;
                return true;
            }
        }
        dynamic_close(h);
    }
    return false;
}

void LadspaHost::Pimpl::unload_plugin()
{
    if (lib_handle != 0)
    {
        LOG_DEBUG("ladspahost: unloading plugin");
        dynamic_close(lib_handle);
        lib_handle = 0;
        descriptor = 0;
        plugin_handle = 0;
    }
}

bool LadspaHost::Pimpl::init_plugin(uint32_t samplerate)
{
    if (lib_handle == 0)
    {
        return false;
    }

    plugin_handle = descriptor->instantiate(descriptor, samplerate);

    if (plugin_handle == 0)
    {
        return false;
    }

}


LadspaHost::LadspaHost() :
    pimpl(new Pimpl)
{}

LadspaHost::~LadspaHost() {} // needed or scoped_ptr may start whining

void LadspaHost::process(AudioStream& stream, const uint32_t frames)
{
    if (source.get() == 0)
    {
        return;
    }

    return source->process(stream, frames);
}

