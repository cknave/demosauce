/*
*   demosauce - fancy icecast source client
*
*   this source is published under the gpl license. google it yourself.
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#ifndef _H_SETTINGS_
#define _H_SETTINGS_

#include <string>
#include <boost/cstdint.hpp>

#include "logror.h"

void init_settings(int argc, char* argv[]);

// seddings, actual instances are in settings.cpp
namespace setting
{
    extern std::string      demovibes_host;
    extern uint32_t         demovibes_port;

    extern std::string      encoder_command;
    extern std::string      encoder_type;
    extern uint32_t         encoder_samplerate;
    extern uint32_t         encoder_bitrate;
    extern uint32_t         encoder_channels;

    extern std::string      cast_host;
    extern uint32_t         cast_port;
    extern std::string      cast_mount;
    extern std::string      cast_user;
    extern std::string      cast_password;
    extern std::string      cast_name;
    extern std::string      cast_url;
    extern std::string      cast_genre;
    extern std::string      cast_description;

    extern std::string      error_tune;
    extern std::string      error_title;
    extern std::string      error_fallback_dir;

    extern std::string      log_file;
    extern logror::Level    log_file_level;
    extern logror::Level    log_console_level;

    extern std::string      debug_song;

#ifdef ENABLE_LADSPA
    extern std::string      ladspa_plugin0;
    extern std::string      ladspa_plugin1;
    extern std::string      ladspa_plugin2;
    extern std::string      ladspa_plugin3;
    extern std::string      ladspa_plugin4;
    extern std::string      ladspa_plugin5;
    extern std::string      ladspa_plugin6;
    extern std::string      ladspa_plugin7;
    extern std::string      ladspa_plugin8;
    extern std::string      ladspa_plugin9;
#endif
}

#endif
