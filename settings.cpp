/*
*   demosauce - fancy icecast source client
*
*   this source is published under the gpl license. google it yourself.
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <cstdlib>
#include <string>
#include <iostream>
#include <fstream>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>

#include "settings.h"

using std::endl;
using std::cout;
using std::string;
using std::ifstream;

using boost::to_lower;

namespace fs = ::boost::filesystem;
namespace po = ::boost::program_options;
namespace log = ::logror;

namespace setting
{
    string      demovibes_host      = "localhost";
    uint32_t    demovibes_port      = 32167;

    string      encoder_command;
    string      encoder_type;
    uint32_t    encoder_samplerate  = 44100;
    uint32_t    encoder_bitrate     = 128;
    uint32_t    encoder_channels    = 2;

    string      cast_host           = "127.0.0.1";
    uint32_t    cast_port           = 8000;
    string      cast_mount          = "stream";
    string      cast_password;
    string      cast_name;
    string      cast_url;
    string      cast_genre;
    string      cast_description;

    string      error_tune;
    string      error_title         = "sorry, we're having some trouble";
    string      error_fallback_dir;

    string      log_file            = "demosauce.log";
    log::Level  log_file_level      = log::info;
    log::Level  log_console_level   = log::warning;

    string      debug_file;

#ifdef ENABLE_LADSPA
    string      ladspa_plugin0;
    string      ladspa_plugin1;
    string      ladspa_plugin2;
    string      ladspa_plugin3;
    string      ladspa_plugin4;
    string      ladspa_plugin5;
    string      ladspa_plugin6;
    string      ladspa_plugin7;
    string      ladspa_plugin8;
    string      ladspa_plugin9;
#endif

}

using namespace setting;

string configFileName = "demosauce.conf";
string castForcePassword;
string logFileLevel;
string logConsoleLevel;

void build_descriptions(po::options_description& settingsDesc, po::options_description& optionsDesc)
{
    settingsDesc.add_options()
    ("demovibes_host", po::value<string>(&demovibes_host))
    ("demovibes_port", po::value<uint32_t>(&demovibes_port))

    ("encoder_command", po::value<string>(&encoder_command))
    ("encoder_type", po::value<string>(&encoder_type))
    ("encoder_samplerate", po::value<uint32_t>(&encoder_samplerate))
    ("encoder_bitrate", po::value<uint32_t>(&encoder_bitrate))
    ("encoder_channels", po::value<uint32_t>(&encoder_channels))

    ("cast_host", po::value<string>(&cast_host))
    ("cast_port", po::value<uint32_t>(&cast_port))
    ("cast_mount", po::value<string>(&cast_mount))
    ("cast_password", po::value<string>(&cast_password))
    ("cast_name", po::value<string>(&cast_name))
    ("cast_url", po::value<string>(&cast_url))
    ("cast_genre", po::value<string>(&cast_genre))
    ("cast_description", po::value<string>(&cast_description))

    ("error_tune", po::value<string>(&error_tune))
    ("error_title", po::value<string>(&error_title))
    ("error_fallback_dir", po::value<string>(&error_fallback_dir))

    ("log_file", po::value<string>(&log_file))
    ("log_file_level", po::value<string>(&logFileLevel))
    ("log_console_level", po::value<string>(&logConsoleLevel))
#ifdef ENABLE_LADSPA
    // maybe there is a better way to do that :)
    ("ladspa_plugin0", po::value<string>(&ladspa_plugin0))
    ("ladspa_plugin1", po::value<string>(&ladspa_plugin1))
    ("ladspa_plugin2", po::value<string>(&ladspa_plugin2))
    ("ladspa_plugin3", po::value<string>(&ladspa_plugin3))
    ("ladspa_plugin4", po::value<string>(&ladspa_plugin4))
    ("ladspa_plugin5", po::value<string>(&ladspa_plugin5))
    ("ladspa_plugin6", po::value<string>(&ladspa_plugin6))
    ("ladspa_plugin7", po::value<string>(&ladspa_plugin7))
    ("ladspa_plugin8", po::value<string>(&ladspa_plugin8))
    ("ladspa_plugin9", po::value<string>(&ladspa_plugin9))
#endif
    ;

    optionsDesc.add_options()
    ("help", "what do you think this flag does? call the police, maybe?")
    ("config_file,c", po::value<string>(&configFileName), "use config file, default: demosauce.conf")
    ("cast_password,p", po::value<string>(&castForcePassword), "password for cast server, overwrites setting from config file")
    ("debug_file,f", po::value<string>(&debug_file), "load specific file, intended for testing and debugging")
    ;
}

void check_sanity()
{
    bool err = false;

    if (demovibes_port < 1 || demovibes_port > 65535)
    {
        err = true;
        cout << "setting demovibes_port out of range (1-65535)\n";
    }

    if (encoder_samplerate <  8000 || encoder_samplerate > 192000)
    {
        err = true;
        cout << "setting encoder_samplerate out of range (8000-192000)\n";
    }

    if (encoder_command.empty())
    {
        err = true;
        cout << "setting encoder_command is not specified\n";
    }

    to_lower(encoder_type);
    if (encoder_type != "mp3")
    {
        err = true;
        cout << "setting encoder_type must be 'mp3'\n";
    }

    if (encoder_bitrate > 10000)
    {
        err = true;
        cout << "setting encoder_bitrate too high >10000\n";
    }

    if (encoder_channels < 1 and encoder_channels > 2)
    {
        err = true;
        cout << "setting encoder_channels out of range (1-2)\n";
    }

    if (cast_port < 1 || cast_port > 65535)
    {
        err = true;
        cout << "setting cast_port out of range (1-65535)\n";
    }

    if (err)
    {
        exit(EXIT_FAILURE);
    }
}

void init_settings(int argc, char* argv[])
{
    po::options_description settingsDesc("settings");
    po::options_description optionsDesc("allowed options");
    build_descriptions(settingsDesc, optionsDesc);

    po::variables_map optionsMap;
    po::store(po::parse_command_line(argc, argv, optionsDesc), optionsMap);
    po::notify(optionsMap);

    if (optionsMap.count("help"))
    {
        cout << optionsDesc;
        exit(EXIT_SUCCESS);
    }

    if (!fs::exists(configFileName))
    {
        cout << "cannot find config file: " << configFileName << endl;
        exit(EXIT_FAILURE);
    }

    ifstream configFile(configFileName.c_str(), ifstream::in);
    if (configFile.fail())
    {
        cout << "failed to read config file: " << configFileName << endl;
        exit(EXIT_FAILURE);
    }

    cout << "reading config file\n";
    po::variables_map settingsMap;
    po::store(po::parse_config_file(configFile, settingsDesc), settingsMap);
    po::notify(settingsMap);

    if (optionsMap.count("cast_password"))
    {
        cast_password = castForcePassword;
    }
    if (settingsMap.count("log_file_level") && !log_string_to_level(logFileLevel, log_file_level))
    {
        cout << "setting log_file_level: unknown level\n";
    }
    if (settingsMap.count("log_console_level") && !log_string_to_level(logConsoleLevel, log_console_level))
    {
        cout << "setting log_console_level: unknown level\n";
    }

    check_sanity();
}

