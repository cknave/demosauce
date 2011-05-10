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

struct PluginChannel
{
    LADSPA_Handle   handle;
    unsigned long   input_port;
    unsigned long   output_port;
};

struct LadspaHost::Pimpl
{
    Pimpl();
    virtual ~Pimpl();

    bool load_plugin(string label);
    void unload_plugin();

    bool is_stereo_plugin();
    bool init_plugin(uint32_t samplerate);

    LIB_HANDLE_T            lib_handle;
    LADSPA_Descriptor*      descriptor;
    vector<PluginChannel>   channels;

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
    reset_channels();
    if (lib_handle != 0)
    {
        LOG_DEBUG("ladspahost: unloading library");
        dynamic_close(lib_handle);
        lib_handle = 0;
        descriptor = 0;
    }
}

LADSPA_Handle LadspaHost::Pimp::init_plugin(unsigned long samplerate)
{
    LADSPA_Handle h = descriptor->instantiate(descriptor, samplerate);

    if (h == 0)
    {
        LOG_ERROR("[ladspahost] failed to init plugin %1%"), descriptor->Label;
        return 0;
    }

    if (descriptor->activate != 0)
    {
        descriptor->activate(h);
    }

    return h;
}

// count port of specific type
unsigned long LadspaHost::Pimpl::count_ports(LADSPA_PortDescriptor port_type)
{
    unsigned long counter = 0;
    for (unsigned long i = 0; i < descriptor->PortCount; i++)
    {
        LADSPA_PortDescriptor p = descriptor->PortDescriptors[i];
        if (p & port_type)
        {
            counter++;
        }
    }
    return counter;
}

// get Nth port of specific type
unsigned long LadspaHost::Pimpl::get_nth_port(LADSPA_Descriptor descriptor, LADSPA_PortDescriptor port_type, unsigned long n)
{
    unsigned long counter = 0;
    for (unsigned long i = 0; i < descriptor->PortCount; i++)
    {
        LADSPA_PortDescriptor p = descriptor->PortDescriptors[i];
        if (p & port_type && counter++ == n)
        {
            return i;
        }
    }
    return 0;
}

bool LadspaHost::Pimpl::is_stereo_plugin()
{
    bool is_stero = count_ports(LADSPA_PORT_AUDIO | LADSPA_PORT_INPUT) == 2
        && count_ports(LADSPA_PORT_AUDIO | LADSPA_PORT_OUTPUT) == 2;
    return is_stero;
}

bool LadspaHost::Pimpl::init_channels(uint32_t samplerate uint32_t channels)
{
    close_channels();

    if (channels == 2 && is_stereo_plugin())
    {
        // we have a stereo plugin
        LOG_INFO("[ladspahost] treating %1% as stereo plugin"), descriptor->Label;

        LADSPA_Handle handle = init_plugin(numeric_cast<unsigned int>(samplerate));

        if (handle == 0)
        {
            return false;
        }

        PluginChannel left = {handle,
            get_nth_port(LADSPA_PORT_AUDIO | LADSPA_PORT_INPUT, 0),
            get_nth_port(LADSPA_PORT_AUDIO | LADSPA_PORT_OUTPUT, 0)};
        PluginChannel right = {handle,
            get_nth_port(LADSPA_PORT_AUDIO | LADSPA_PORT_INPUT, 1),
            get_nth_port(LADSPA_PORT_AUDIO | LADSPA_PORT_OUTPUT, 1)};

        channels.push_back(left);
        channels.push_back(right);
    }
    else
    {
        // we have a mono plugin!
        LOG_INFO("[ladspahost] treating %1% as mono plugin"), descriptor->Label;

        for (uint32_t i = 0; i < channels; i++)
        {
            LADSPA_Handle handle = init_plugin(numeric_cast<unsigned int>(samplerate));

            if (handle == 0)
            {
                return false;
            }

            PluginChannel channel = {handle,
                get_nth_port(LADSPA_PORT_AUDIO | LADSPA_PORT_INPUT, 0),
                get_nth_port(LADSPA_PORT_AUDIO | LADSPA_PORT_OUTPUT, 0)};

            channels.push_back(channel);
        }
    }

    return true;
}

void LadspaHost::Pimpl::close_channels()
{
    if (channels.empty())
    {
        return;
    }

    LOG_DEBUG("ladspahost: decativating %1%"), descriptor->Label;

    if (is_stereo_plugin())
    {
        PluginChannel channel = channels[0];
        if (descriptor->deactivate != 0)
        {
            descriptor->deactivate(channel.handle);
        }
        descriptor->cleanup(channel.handle);
    }
    else
    {
        BOOST_FOREACH(PluginChannel channel, channels)
        {
            if (descriptor->deactivate != 0)
            {
                descriptor->deactivate(channel.handle);
            }
            descriptor->cleanup(channel.handle);
        }
    }
    channels.clear();
}

LadspaHost::LadspaHost() :
    pimpl(new Pimpl)
{}

void LadspaHost::Pimpl::process(AudioStream& stream, const uint32_t frames)
{
    if (stream.samplerate() != samplerate || stream.channels() != channels.size())
    {
        close_channels();
        init_channels(stream.samplerate(), stream.channels());
    }

    unsigned long stream_frames = numeric_cast<unsigned long>(stream.frames());
    for (size_t i = 0; i < cannels.size(); i++)
    {
        PluginChannel c = channels[i];
        descriptor->connect_port(c.handle, c.input_port, stream.buffer(i));
        descriptor->connect_port(c.handle, c.output_port, stream.buffer(i))
        descriptor->Run(c.handle, stream_frames);
    }
}

LadspaHost::~LadspaHost() {} // needed or scoped_ptr may start whining

void LadspaHost::process(AudioStream& stream, const uint32_t frames)
{
    if (source.get() == 0 || pimpl->descriptor == 0)
    {
        return;
    }
    source->process(stream, frames);
    pimpl->run(stream, frames);
}
