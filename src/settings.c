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

static int      settings_config_version     = 34;
const char*     settings_demovibes_host     = "localhost";
int             settings_demovibes_port     = 32167;
const char*     settings_encoder_command;
int             settings_encoder_samplerate = 44100;
int             settings_encoder_bitrate    = 224;
int             settings_encoder_channels   = 2;
const char*     settings_cast_host          = "localhost";
int             settings_cast_port          = 8000; 
const char*     settings_cast_mount         = "stream";
const char*     settings_cast_user          = "soruce";
const char*     settings_cast_password;
const char*     settings_cast_name;
const char*     settings_cast_url;
const char*     settings_cast_genre;
const char*     settings_cast_description;
int             settings_decode_buffer_size = 200;
const char*     settings_error_title        = "sorry, out of juice";
const char*     settings_log_file           = "demosauce.log";
enum log_level  settings_log_file_level     = log_info;
enum log_level  settings_log_console_level  = log_warn;
const char*     settings_debug_song;

static const char* config_file_name = "demosauce.conf";

static void die(const char* msg)
{
    puts(msg);
    exit(EXIT_FAILURE);
}

static void read_config(void)
{
    #define GETINT(key) settings_##key = keyval_int(buffer, #key, settings_##key)
    #define GETSTR(key) settings_##key = keyval_str(NULL, 0, buffer, #key, settings_##key)
    
    FILE* f = fopen(config_file_name, "r"); 
    
    if (!f) 
        die("cat't read config file");

    fseek(f, 0, SEEK_END);
    size_t bsize = ftell(f);
    rewind(f);
    char* buffer = util_malloc(bsize + 1);
    fread(buffer, 1, bsize, f);
    fclose(f);
    buffer[bsize] = 0;

    GETINT(config_version);
    GETSTR(demovibes_host);
    GETINT(demovibes_port);
    GETINT(encoder_samplerate);
    GETINT(encoder_bitrate);
    GETINT(encoder_channels);
    GETSTR(cast_host);
    GETINT(cast_port);
    GETSTR(cast_mount);
    GETSTR(cast_user);
    GETSTR(cast_password);
    GETSTR(cast_name);
    GETSTR(cast_url);
    GETSTR(cast_genre);
    GETSTR(cast_description);
    GETINT(decode_buffer_size);
    GETSTR(error_title);
    GETSTR(log_file);

    char tmpstr[8] = {0};
    keyval_str(tmpstr, 8, buffer, "log_file_level", "");
    log_string_to_level(tmpstr, &settings_log_file_level);
    keyval_str(tmpstr, 8, buffer, "log_console_level", "");
    log_string_to_level(tmpstr, &settings_log_console_level);

    util_free(buffer);
}

static void check_sanity(void)
{
    if (settings_config_version != 34)
        die("your config file is outdated, need config_version 34");

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

    if (settings_decode_buffer_size < 1 || settings_decode_buffer_size > 10000) 
        die("setting decode_buffer_size out of range (1-10000)");
}

#define HELP_MESSAGE DEMOSAUCE_VERSION"\n\t-h print help\n\t-c <path> config file\n\t-d <options> debug options\n\t-V print version\n"

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

