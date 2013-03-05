/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#ifndef SETTINGS_H
#define SETTINGS_H

#include "logror.h"

void settings_init(int argc, char** argv);

extern const char*      settings_demovibes_host;
extern int              settings_demovibes_port;
extern const char*      settings_encoder_command;
extern int              settings_encoder_samplerate;
extern int              settings_encoder_bitrate;
extern int              settings_encoder_channels;
extern const char*      settings_cast_host;
extern int              settings_cast_port;
extern const char*      settings_cast_mount;
extern const char*      settings_cast_user;
extern const char*      settings_cast_password;
extern const char*      settings_cast_name;
extern const char*      settings_cast_url;
extern const char*      settings_cast_genre;
extern const char*      settings_cast_description;
extern int              settings_decode_buffer_size;
extern const char*      settings_error_tune;
extern const char*      settings_error_title;
extern const char*      settings_error_fallback_dir;
extern const char*      settings_log_file;
extern enum log_level   settings_log_file_level;
extern enum log_level   settings_log_console_level;
extern const char*      settings_debug_song;

#endif

