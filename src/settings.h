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

#include <string>
#include "logror.h"

void settings_init(int argc, char** argv);

extern std::string  settings_demovibes_host;
extern int          settings_demovibes_port;

extern std::string  settings_encoder_command;
extern std::string  settings_encoder_type;
extern int          settings_encoder_samplerate;
extern int          settings_encoder_bitrate;
extern int          settings_encoder_channels;

extern std::string  settings_cast_host;
extern int          settings_cast_port;
extern std::string  settings_cast_mount;
extern std::string  settings_cast_user;
extern std::string  settings_cast_password;
extern std::string  settings_cast_name;
extern std::string  settings_cast_url;
extern std::string  settings_cast_genre;
extern std::string  settings_cast_description;

extern int          settings_decode_buffer_size;

extern std::string  settings_error_tune;
extern std::string  settings_error_title;
extern std::string  settings_error_fallback_dir;

extern std::string  settings_log_file;
extern LogLevel     settings_log_file_level;
extern LogLevel     settings_log_console_level;

extern std::string  settings_debug_song;

#endif
