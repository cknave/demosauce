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
#include "util.h"
#include "settings.h"

#define X(type, key, value) SETTINGS_##type settings_##key = value;
SETTINGS_LIST
#undef X

static const char* config_file_name = "demosauce.conf";

static void die(const char* msg)
{
    puts(msg);
    exit(EXIT_FAILURE);
}

static void read_config(void)
{
    FILE* f = fopen(config_file_name, "r"); 
    if (!f) 
        die("cat't read config file");

    fseek(f, 0, SEEK_END);
    size_t bsize = ftell(f);
    rewind(f);
    char* buffer = util_malloc(bsize + 1);
    fread(buffer, 1, bsize, f);
    buffer[bsize] = 0;
    fclose(f);
    
    char tmpstr[8] = {0};
    #define GET_int(key) settings_##key = keyval_int(buffer, #key, settings_##key);
    #define GET_str(key) settings_##key = keyval_str(NULL, 0, buffer, #key, settings_##key);
    #define GET_log(key) keyval_str(tmpstr, sizeof(tmpstr), buffer, #key, NULL); \
                         log_string_to_level(tmpstr, &settings_##key);
    #define X(type, key, value) GET_##type(key)
    SETTINGS_LIST
    #undef X

    util_free(buffer);
}

static void check_sanity(void)
{
    if (settings_config_version != 34)
        die("config file seems to be outdated, need config_version 34");

    if (settings_demovibes_port < 1 || settings_demovibes_port > 65535) 
        die("setting demovibes_port out of range (1-65535)");

    if (settings_encoder_samplerate <  8000 || settings_encoder_samplerate > 192000) 
        die("setting encoder_samplerate out of range (8000-192000)");

    if (settings_encoder_bitrate > 10000)
        die("setting encoder_bitrate too high >10000");

    if (settings_encoder_channels < 1 || settings_encoder_channels > 2)
        die("setting encoder_channels out of range (1-2)");

    if (settings_cast_port < 1 || settings_cast_port > 65535) 
        die("setting cast_port out of range (1-65535)");
}

#define HELP_MESSAGE "syntax: demosauce [options]\n\t-h print help\n\t-c <path> config file\n\t-d <options> debug options\n\t-V print version\n"

void settings_init(int argc, char** argv)
{
    char c = 0;
    while ((c = getopt(argc, argv, "hc:d:V")) != -1) {
        switch (c) {
        default:
        case '?':
            if (strchr("cd", optopt))
                puts("expecting argument");
            puts(HELP_MESSAGE);
            exit(EXIT_FAILURE);
        case 'h':
            puts(HELP_MESSAGE);
            exit(EXIT_SUCCESS);
        case 'c':
            config_file_name = optarg;            
            break;
        case 'd':
            settings_debug_song = optarg;
            break;
        case 'V':
            puts(DEMOSAUCE_VERSION);
            exit(EXIT_SUCCESS);
        }
    }

    read_config();
    check_sanity();
}

