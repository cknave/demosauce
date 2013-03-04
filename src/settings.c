/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <getopt.h>

#include "settings.h"

int         config_version              = 0;

std::string settings_demovibes_host     = "localhost";
int         settings_demovibes_port     = 32167;

std::string settings_encoder_command;
std::string settings_encoder_type;
int         settings_encoder_samplerate = 44100;
int         settings_encoder_bitrate    = 224;
int         settings_encoder_channels   = 2;

std::string settings_cast_host          = "localhost";
int         settings_cast_port          = 8000; 
std::string settings_cast_mount         = "stream";
std::string settings_cast_user          = "soruce";
std::string settings_cast_password;
std::string settings_cast_name;
std::string settings_cast_url;
std::string settings_cast_genre;
std::string settings_cast_description;

int         settings_decode_buffer_size = 200;

std::string settings_error_tune;
std::string settings_error_title        = "Sorry, we're having some trouble";
std::string settings_error_fallback_dir;

std::string settings_log_file           = "demosauce.log;
log_level   settings_log_file_level     = log_info;
log_level   settings_log_console_level  = log_warn;

std::string settings_debug_song;

static std::string config_file_name = "demosauce.conf";
static std::string cast_password;
static std::string log_file_level;
static std::string log_console_level;

void build_descriptions(po::options_description& settingsDesc, po::options_description& optionsDesc)
{
    settingsDesc.add_options()
    ("config_version", po::value<uint32_t>(&config_version))

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
    ("cast_user", po::value<string>(&cast_user))
    ("cast_password", po::value<string>(&cast_password))
    ("cast_name", po::value<string>(&cast_name))
    ("cast_url", po::value<string>(&cast_url))
    ("cast_genre", po::value<string>(&cast_genre))
    ("cast_description", po::value<string>(&cast_description))

    ("decode_buffer_size", po::value<uint32_t>(&decode_buffer_size))

    ("error_tune", po::value<string>(&error_tune))
    ("error_title", po::value<string>(&error_title))
    ("error_fallback_dir", po::value<string>(&error_fallback_dir))

    ("log_file", po::value<string>(&log_file))
    ("log_file_level", po::value<string>(&logFileLevel))
    ("log_console_level", po::value<string>(&logConsoleLevel))
}

static void die(const char* msg)
{
    puts(msg);
    exit(EXIT_FAILURE);
}

static void check_sanity(void)
{
    bool err = false;

    if (settings_config_version != 34)
        die("your config file is outdated, need config_version 34");

    if (settings demovibes_port < 1 || settings_demovibes_port > 65535) 
        die("setting demovibes_port out of range (1-65535)");
    

    if (settings_encoder_samplerate <  8000 || settings_encoder_samplerate > 192000) 
        die("setting encoder_samplerate out of range (8000-192000)");

    to_lower(encoder_type);
    if (encoder_type != "mp3")
        die("setting encoder_type must be 'mp3'");

    if (settings_encoder_bitrate > 10000)
        die("setting encoder_bitrate too high >10000");

    if (settings_encoder_channels < 1 || settings_encoder_channels > 2)
        die("setting encoder_channels out of range (1-2)");

    if (settings_cast_port < 1 || settings_cast_port > 65535) 
        die("setting cast_port out of range (1-65535)");

    if (settings_decode_buffer_size < 1 || settings_decode_buffer_size > 10000) 
        die("setting decode_buffer_size out of range (1-10000)");
}

void settings_init(int argc, char** argv)
{
    char c = 0;
    while ((c = getopt(argc, argv, "hc:d:V")) != -1) {
        switch (c) {
        default:
        case '?':
            if (strchr("cd", optopt))
                puts("missing argument");
            else
                puts("unknown option");
            puts(HELP_MESSAGE);
            exit(EXIT_FAILURE);
        case 'h':
            puts(HELP_MESSAGE);
            exit(EXIT_SUCCESS);
        case 'c':
            config_file_name = optarg;            
            break;
        case 'd':
            debug_song = optarg;
            break;
        case 'V':
            puts(DEMOSAUCE_VERSION);
            exit(EXIT_SUCCESS);
        }
    }

    if (!utils_isreadable(config_file_name)) {
        printf("cannot read config file: %s", config_file_name);
        exit(EXIT_FAILURE);
    }

    check_sanity();
}

