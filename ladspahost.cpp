/*
*   demosauce - icecast source client
*
*   this source is published under the gpl license. google it yourself.
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include "logror.h"
#include "ladspahost.h"

#if defined (__unix__)
    #include <dlfcn.h>
    #define LIB_HANDLE_T        void*
    #define PLUGIN_EXTENSION    ".so"
    #define PATHENV_SEPARATOR   ":"
    #define SEARCH_DIR_0        "/usr/lib/ladspa/"
#elif defined (_WIN32)
    #include <windows.h>
    #define LIB_HANDLE_T        HWND
    #define PLUGIN_EXTENSION    ".dll"
    #define PATHENV_SEPARATOR   ";"
    #error "LADSPA on windows still needs some work, DO IT!"
#else
    #error "LADSPA won't work on your system. disable or implement it"
#endif

using std::string;
using std::vector;

using boost::iends_with;
using boost::numeric_cast;
using boost::lexical_cast;

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

void ladspa_enumerate_plugins(vector<string>& list)
{
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
}

LIB_HANDLE_T ladspa_load_impl(string path, vector<const LADSPA_Descriptor*>& desc)
{
    LIB_HANDLE_T h = dynamic_open(path);
    if (h == 0)
    {
        return 0;
    }

    LADSPA_Descriptor_Function df = bind_descriptor_function(h);
    if (df == 0)
    {
        dynamic_close(h);
        return 0;
    }

    for (unsigned long i = 0; true; i++)
    {
        const LADSPA_Descriptor* d = df(i);
        if (d == 0)
        {
            break;
        }
        desc.push_back(d);
    }

    return h;
}

LadspaHandle ladspa_load(string path, vector<const LADSPA_Descriptor*>& desc)
{
    return static_cast<LadspaHandle>(ladspa_load_impl(path, desc));
}

void ladspa_unload(LadspaHandle handle)
{
    dynamic_close(static_cast<LIB_HANDLE_T>(handle));
}

LADSPA_Data ladspa_default_value(LADSPA_PortRangeHint hint)
{
    if (LADSPA_IS_HINT_HAS_DEFAULT(hint.HintDescriptor))
    {
        if (LADSPA_IS_HINT_DEFAULT_MINIMUM(hint.HintDescriptor))
        {
            return hint.LowerBound;
        }
        if (LADSPA_IS_HINT_DEFAULT_LOW(hint.HintDescriptor))
        {
            return LADSPA_IS_HINT_LOGARITHMIC(hint.HintDescriptor) ?
                exp(log(hint.LowerBound) * .75 + log(hint.UpperBound) * .25) :
                (hint.LowerBound * .75 + hint.UpperBound * .25);
        }
        if (LADSPA_IS_HINT_DEFAULT_MIDDLE(hint.HintDescriptor))
        {
            return LADSPA_IS_HINT_LOGARITHMIC(hint.HintDescriptor) ?
                exp(log(hint.LowerBound) * .5 + log(hint.UpperBound) * .5) :
                (hint.LowerBound * .5 + hint.UpperBound * .5);
        }
        if (LADSPA_IS_HINT_DEFAULT_HIGH(hint.HintDescriptor))
        {
            return LADSPA_IS_HINT_LOGARITHMIC(hint.HintDescriptor) ?
                exp(log(hint.LowerBound) * .25 + log(hint.UpperBound) * .75) :
                (hint.LowerBound * .25 + hint.UpperBound * .75);
        }
        if (LADSPA_IS_HINT_DEFAULT_MAXIMUM(hint.HintDescriptor))
        {
            return hint.UpperBound;
        }
        if (LADSPA_IS_HINT_DEFAULT_0(hint.HintDescriptor))
        {
            return 0;
        }
        if (LADSPA_IS_HINT_DEFAULT_1(hint.HintDescriptor))
        {
            return 1;
        }
        if (LADSPA_IS_HINT_DEFAULT_100(hint.HintDescriptor))
        {
            return 100;
        }
        if (LADSPA_IS_HINT_DEFAULT_440(hint.HintDescriptor))
        {
            return 440;
        }
    }
    return 0;
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

    // management
    bool            load_plugin(string label);
    void            unload_plugin();
    LADSPA_Handle   instantiate(uint32_t samplerate);
    bool            update_channels(uint32_t samplerate, uint32_t channel_list);
    void            close_channels();
    void            configure(const vector<Setting>& settings);

    // information
    unsigned long   count_ports(LADSPA_PortDescriptor port_type);
    unsigned long   get_nth_port(LADSPA_PortDescriptor port_type, unsigned long n);
    bool            is_stereo_plugin();
    bool            is_inplace_broken();

    // run the sucker
    void            process(AudioStream& input_stream, AudioStream& output_stream, uint32_t frames);

    // variables
    uint32_t                    samplerate;
    LIB_HANDLE_T                lib_handle;
    const LADSPA_Descriptor*    descriptor;
    vector<PluginChannel>       channel_list;
    vector<LADSPA_Data>         setting_list;
    AudioStream                 source_stream;

};

LadspaHost::Pimpl::Pimpl() :
    samplerate(44100),
    lib_handle(0),
    descriptor(0)
{
}

LadspaHost::Pimpl::~Pimpl()
{
    unload_plugin();
}

bool LadspaHost::Pimpl::load_plugin(string label)
{
    unload_plugin();
    vector<string> plugin_list;
    ladspa_enumerate_plugins(plugin_list);
    BOOST_FOREACH(string file_name, plugin_list)
    {
        vector<const LADSPA_Descriptor*> desc_list;
        LOG_DEBUG("ladspahost: inspecting %1%"), file_name;

        LIB_HANDLE_T h = ladspa_load(file_name, desc_list);
        if (h == 0)
        {
            LOG_DEBUG("ladspahost: can't open %1%"), file_name;
            return false;
        }

        if (desc_list.empty())
        {
            LOG_DEBUG("ladspahost: not a LADSPA plugin %1%"), file_name;
            dynamic_close(h);
            continue;
        }

        BOOST_FOREACH(const LADSPA_Descriptor* d, desc_list)
        {
            if (label == d->Label)
            {
                LOG_INFO("ladspahost: loading %1% (%2%)"), label, file_name;
                lib_handle = h;
                descriptor = d;
                return true;
            }
        }
        // in case it's not the droid we're looking for
        dynamic_close(h);
    }
    return false;
}

void LadspaHost::Pimpl::unload_plugin()
{
    close_channels();
    if (lib_handle != 0)
    {
        LOG_DEBUG("ladspahost: unloading library");
        dynamic_close(lib_handle);
        lib_handle = 0;
        descriptor = 0;
    }
}

LADSPA_Handle LadspaHost::Pimpl::instantiate(uint32_t samplerate)
{
    unsigned long samplerate0 = numeric_cast<unsigned long>(samplerate);
    LADSPA_Handle h = descriptor->instantiate(descriptor, samplerate0);

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

bool LadspaHost::Pimpl::update_channels(uint32_t samplerate0, uint32_t channels0)
{
    if (samplerate0 == samplerate && channels0 == channel_list.size())
    {
        return true;
    }

    close_channels();

    if (channels0 == 2 && is_stereo_plugin())
    {
        // we have a stereo plugin
        LOG_INFO("[ladspahost] treating %1% as stereo plugin"), descriptor->Label;

        LADSPA_Handle handle = instantiate(samplerate0);

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

        channel_list.push_back(left);
        channel_list.push_back(right);
    }
    else
    {
        // we have a mono plugin!
        LOG_INFO("[ladspahost] treating %1% as mono plugin"), descriptor->Label;

        for (uint32_t i = 0; i < channels0; i++)
        {
            LADSPA_Handle handle = instantiate(samplerate0);

            if (handle == 0)
            {
                return false;
            }

            PluginChannel channel = {handle,
                get_nth_port(LADSPA_PORT_AUDIO | LADSPA_PORT_INPUT, 0),
                get_nth_port(LADSPA_PORT_AUDIO | LADSPA_PORT_OUTPUT, 0)};

            channel_list.push_back(channel);
        }
    }

    samplerate = samplerate0;
    return true;
}

void LadspaHost::Pimpl::close_channels()
{
    if (channel_list.empty())
    {
        return;
    }

    LOG_DEBUG("ladspahost: decativating %1%"), descriptor->Label;

    if (is_stereo_plugin())
    {
        PluginChannel channel = channel_list[0];
        if (descriptor->deactivate != 0)
        {
            descriptor->deactivate(channel.handle);
        }
        descriptor->cleanup(channel.handle);
    }
    else
    {
        BOOST_FOREACH(PluginChannel channel, channel_list)
        {
            if (descriptor->deactivate != 0)
            {
                descriptor->deactivate(channel.handle);
            }
            descriptor->cleanup(channel.handle);
        }
    }
    channel_list.clear();
}

unsigned long LadspaHost::Pimpl::count_ports(LADSPA_PortDescriptor port_type)
{
    // count port of specific type
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

unsigned long LadspaHost::Pimpl::get_nth_port(LADSPA_PortDescriptor port_type, unsigned long n)
{
    // get Nth port of specific type
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
bool LadspaHost::Pimpl::is_inplace_broken()
{
    return LADSPA_IS_INPLACE_BROKEN(descriptor->Properties);
}

void LadspaHost::Pimpl::configure(const vector<Setting>& settings)
{
    setting_list.resize(descriptor->PortCount);
    std::fill(setting_list.begin(), setting_list.end(), 0);

    BOOST_FOREACH(Setting value, settings)
    {
        if (value.first < setting_list.size())
        {
            setting_list[value.first] = value.second;
        }
    }

    for (unsigned long i = 0; i < descriptor->PortCount; i++)
    {
        if (setting_list[i] == 0 && LADSPA_IS_PORT_CONTROL(descriptor->PortDescriptors[i]))
        {
            setting_list[i] = ladspa_default_value(descriptor->PortRangeHints[i]);
        }
    }
}

void LadspaHost::Pimpl::process(AudioStream& input_stream, AudioStream& output_stream, uint32_t frames)
{
    unsigned long stream_frames = numeric_cast<unsigned long>(input_stream.frames());
    for (size_t i = 0; i < channel_list.size(); i++)
    {
        PluginChannel c = channel_list[i];
        descriptor->connect_port(c.handle, c.input_port, input_stream.buffer(i));
        descriptor->connect_port(c.handle, c.output_port, output_stream.buffer(i));
        descriptor->run(c.handle, stream_frames);
    }
}

LadspaHost::LadspaHost() :
    pimpl(new Pimpl)
{}
LadspaHost::~LadspaHost()
{
    pimpl->unload_plugin();
}

void LadspaHost::process(AudioStream& stream, uint32_t frames)
{
    if (source.get() == 0 || pimpl->descriptor == 0)
    {
        return;
    }

    bool use_inplace = !pimpl->is_inplace_broken();

    if (use_inplace)
    {
        source->process(stream, frames);
    }
    else
    {
        source->process(pimpl->source_stream, frames);
        if (stream.max_frames() < pimpl->source_stream.frames())
        {
            stream.resize(pimpl->source_stream.frames());
        }
        stream.set_frames(pimpl->source_stream.frames());
        stream.end_of_stream = pimpl->source_stream.end_of_stream;
    }

    if (!pimpl->update_channels(pimpl->samplerate, stream.channels()))
    {
        LOG_ERROR("[ladspahost] failed to initialize channel_list %1%"), pimpl->descriptor->Label;
        pimpl->unload_plugin();
        return;
    }

    if (use_inplace)
    {
        pimpl->process(stream, stream, frames);
    }
    else
    {
        pimpl->process(pimpl->source_stream, stream, frames);
    }
}

bool LadspaHost::load_plugin(string label, uint32_t samplerate, const vector<Setting>& setting_list)
{
    pimpl->samplerate = samplerate;
    bool loaded = pimpl->load_plugin(label);
    if (loaded)
    {
        pimpl->configure(setting_list);
        LOG_INFO("[ladspahost] %1% loaded"), label;
    }
    else
    {
        LOG_ERROR("[ladspahost] %1% failed to load"), label;
    }
    return loaded;
}

bool LadspaHost::load_plugin(string descriptor, uint32_t samplerate)
{
    vector<string> elements;
    boost::split(elements, descriptor, boost::is_any_of(" :"));

    if (elements.empty())
    {
        return false;
    }

    vector<Setting> settings;
    Setting setting;

    for (size_t i = 1; i < elements.size(); i++)
    {
        try
        {
            if (i % 2 == 1)
            {
                setting.first = lexical_cast<unsigned long>(elements[i]);
            }
            else
            {
                setting.second = lexical_cast<double>(elements[i]);
                settings.push_back(setting);
            }
        }
        catch (boost::bad_lexical_cast& e)
        {
            LOG_WARNING("[ladspahost] bad setting value (%1%)"), e.what();
            i += i % 2;
        }
    }

    return load_plugin(elements[0], samplerate, settings);
}

